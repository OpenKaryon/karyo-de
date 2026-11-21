# SPDX-License-Identifier: GPL-3.0-or-later
"""Return only from the exited profile's still-active native console."""
from dataclasses import asdict
import time

from .consoles import KernelConsoles
from .logind import SessionError
from .switching import ExistingSwitcher


class ReturnAfterExit:
    def __init__(self,store,accounts,sessions,*,kernel=None,clock=time.monotonic,sleep=time.sleep):
        self.store,self.accounts,self.sessions=store,accounts,sessions
        self.kernel=KernelConsoles() if kernel is None else kernel
        self.clock,self.sleep=clock,sleep

    def seat_owned(self,graph):
        if self.kernel.state().active!=graph.vt:return False
        active=self.sessions.seat_active()
        if active and active!=graph.identity.login.id:return False
        if active and self.sessions.owner(active).identity!=graph.identity.login.identity:return False
        return True

    def eligible(self,account):
        if self.accounts.by_uid(account.uid)!=account:return False
        expiry=self.accounts.expiry_day(account.name);hashed=self.accounts.password_hash(account.name)
        return (expiry is None or expiry>int(time.time()//86400)) and bool(hashed) and not hashed.startswith(("!","*"))

    def run(self,scope):
        if not self.store._locked:raise PermissionError("Profile return requires the configuration lock")
        graph=scope.graph
        if graph.pidfd is None or not graph.process.exited(graph.pidfd):
            raise PermissionError("Profile return requires its captured exited compositor")
        record=scope.journal.record(scope.token)
        if (record["session"]["graphical"] is None or asdict(self.accounts.by_uid(graph.account.uid))!=record["account"]
                or record["session"]["graphical"]["pid"]!=graph.identity.pid
                or record["session"]["graphical"]["start"]!=graph.identity.start
                or record["session"]["graphical"]["invocation"]!=graph.identity.invocation):
            raise PermissionError("The exited profile receipt changed")
        ownership=self.store.ownership();home=ownership.owner(graph.account.name)
        if home==graph.account.name:raise PermissionError("The exited profile has no owning Home account")
        with self.kernel:
            if not self.seat_owned(graph):return {"changed":False,"reason":"seat-changed"}
            account=self.accounts.account(home)
            candidates=[r for r in self.sessions.all() if r.name==home and r.uid==account.uid]
            if len(candidates)>1:raise SessionError("The Home return session is ambiguous")
            greeter=False
            if candidates and self.eligible(account):target=candidates[0]
            else:
                candidates=self.sessions.greeters()
                if len(candidates)!=1:raise SessionError("A unique native Home or greeter is required for return")
                target=candidates[0];greeter=True
            if target.active or target.vt==graph.vt:
                raise SessionError("The return destination conflicts with the exited profile console")
            deadline=self.clock()+8
            switcher=ExistingSwitcher(self.store,accounts=self.accounts,sessions=self.sessions,clock=self.clock,sleep=self.sleep)
            protected=not greeter and self.store.settings(home).password!="none"
            if protected:switcher.lock_ready(target,deadline)
            current=self.sessions.greeter(target.id,timeout=switcher.remaining(deadline)) if greeter else switcher.read_same(target,deadline)
            if (current is None or ExistingSwitcher.identity(current)!=ExistingSwitcher.identity(target)
                    or current.active or protected and not current.locked
                    or self.store.ownership()!=ownership or not greeter and not self.eligible(account)):
                raise SessionError("The return destination changed before activation")
            if not self.seat_owned(graph):return {"changed":False,"reason":"seat-changed"}
            self.sessions.activate(target.id,timeout=switcher.remaining(deadline))
            while True:
                current=self.sessions.greeter(target.id,timeout=switcher.remaining(deadline)) if greeter else switcher.read_same(target,deadline)
                if (current is None or ExistingSwitcher.identity(current)!=ExistingSwitcher.identity(target)):
                    raise SessionError("The activated return destination changed")
                if current.active and self.kernel.state().active==target.vt and self.sessions.seat_active()==target.id:
                    return {"changed":True,"user":target.name,"vt":target.vt,"greeter":greeter}
                switcher.remaining(deadline)
                self.sleep(.05)
