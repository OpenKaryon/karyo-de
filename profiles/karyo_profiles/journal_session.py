# SPDX-License-Identifier: GPL-3.0-or-later
"""Own and reattach the prepared manager plus a fixed graphical PAM session."""
from dataclasses import asdict
import os
import sys
from pathlib import Path
import time

from .consoles import reserve_console
from .credentials import LocalAccounts
from .graphical import GraphicalIdentity,GraphicalSession
from .journal import JournalRetained
from .journal_dns import JournalDNS,recover_profiles,unit_receipt
from .journal_network import JournalNetwork,restore_network,offline
from .journal_netbird import restore_optional,restore_offline_services
from .journal_session_record import owner,validate_session
from .logind import Sessions
from .services import UnitIdentity
from .storage import trusted_directory
from .user_manager import UserManagerScope


def manager_receipt(manager):
    manager.guard();manager.verify_file();manager.verify_policy(manager.info())
    live=None
    if manager.identity is not None:
        manager.verify()
        value=manager.identity
        live={"invocation":value.invocation,"pid":value.pid,"start":value.start,"exe":list(manager.executable),
              "login":asdict(manager.login),"manager_login":asdict(manager.manager_login)}
    return {"directory":list(manager.directory_identity),"file":list(manager.file_identity),
            "dropins":sorted(manager.initial_dropins),"live":live}


def graphical_receipt(graph):
    graph.verify();value=graph.identity
    return {"invocation":value.invocation,"pid":value.pid,"start":value.start,"login":asdict(value.login),
            "scope_invocation":value.scope_invocation,"vt":graph.vt,"waiter":list(graph.waiter),
            "compositor":list(graph.compositor),"entered":graph.entered}


def restore_manager(files,proof,*,netbird=False,catalog=False):
    manager=UserManagerScope(files,netbird=netbird,catalog=catalog)
    manager.initial_dropins=frozenset(proof["dropins"])
    manager.directory_identity=tuple(proof["directory"]);manager.file_identity=tuple(proof["file"])
    try:
        manager.fd=trusted_directory(manager.directory);manager.created=True
        info=os.fstat(manager.fd)
        if (info.st_dev,info.st_ino)!=manager.directory_identity:
            raise JournalRetained("The captured manager directory was replaced")
        manager.verify_file();manager.verify_policy(manager.info())
        live=proof["live"]
        if live is not None:
            cgroup="/user.slice/user-"+str(manager.account.uid)+".slice/"+manager.name+"/init.scope"
            manager.identity=UnitIdentity(live["invocation"],live["pid"],cgroup,live["start"])
            manager.executable=tuple(live["exe"])
            manager.login=owner(live["login"],manager.account)
            manager.manager_login=owner(live["manager_login"],manager.account,manager=True)
            manager.pidfd=manager.process.open(live["pid"])
            manager.verify()
        elif not manager.inactive(manager.info()) or manager.sessions.owners(manager.account.uid):
            raise JournalRetained("The prepared journal manager unexpectedly started")
        return manager
    except BaseException:
        if manager.pidfd is not None:manager.process.close(manager.pidfd);manager.pidfd=None
        if manager.fd is not None:os.close(manager.fd);manager.fd=None
        raise


def restore_graph(manager,proof):
    graph=GraphicalSession(manager)
    login=owner(proof["login"],manager.account)
    cgroup="/user.slice/user-"+str(manager.account.uid)+".slice/"+login.scope
    graph.identity=GraphicalIdentity(proof["invocation"],proof["pid"],proof["start"],login,
                                     proof["scope_invocation"],cgroup)
    graph.invocation,graph.pid,graph.vt=proof["invocation"],proof["pid"],proof["vt"]
    graph.waiter,graph.compositor=tuple(proof["waiter"]),tuple(proof["compositor"])
    graph.attempted=True;graph.entered=proof["entered"]
    try:
        graph.pidfd=graph.process.open(graph.pid);graph.verify()
        current=graph.sessions.get(login.id)
        if (current is None or current.uid!=manager.account.uid or current.name!=manager.account.name or current.vt!=graph.vt):
            raise JournalRetained("The captured graphical login changed")
        deadline=time.monotonic()+2
        while True:
            if graph.compositor_ready():
                graph.entered=True
                break
            if graph.waiting():break
            if time.monotonic()>=deadline:raise JournalRetained("The captured session executable is not a trusted entry or compositor")
            time.sleep(.05)
        expected=os.stat("/run/"+manager.files.plan.runtime_name)
        mounted=os.stat("/proc/"+str(graph.pid)+"/root/run/systemd/resolve")
        if (expected.st_dev,expected.st_ino)!=(mounted.st_dev,mounted.st_ino):
            raise JournalRetained("The graphical resolver mount identity changed")
        return graph
    except BaseException:
        if graph.pidfd is not None:graph.process.close(graph.pidfd);graph.pidfd=None
        raise


def stat_start(pid):
    text=Path("/proc",str(pid),"stat").read_text()
    unused,separator,tail=text.rpartition(") ");fields=tail.split()
    if not separator or len(fields)<20 or not fields[19].isdigit():
        raise JournalRetained("Invalid namespace consumer process identity")
    return int(fields[19])


class JournalSession:
    def __init__(self,journal,dns,manager,graph=None,*,forwarding=None,provider=None):
        self.journal,self.dns,self.manager,self.graph=journal,dns,manager,graph
        self.network=dns.network;self.token=dns.token
        self.forwarding,self.provider=forwarding,provider

    def update(self,**values):
        before=self.journal.record(self.token)
        self.journal.change(before,dict(before,session=dict(before["session"],**values)))

    @classmethod
    def start(cls,journal,dns):
        record=journal.record(dns.token)
        if "session" in record or record["dns"]["pending"] is not None:
            raise JournalRetained("Session startup requires its acknowledged DNS resources")
        dns.services.verify()
        from .journal_forwarding import JournalForwarding
        forwarding=JournalForwarding.restore(journal,dns.network) if "forwarding" in record else None
        manager=UserManagerScope(dns.files,netbird="provider" in record,catalog="provider" in record)
        scope=cls(journal,dns,manager,forwarding=forwarding)
        try:
            scope.provider=restore_optional(journal,dns)
            session={"pending":"manager-prepare","manager":None,"graphical":None}
            if "provider" in record: session["catalog"]="netbird"
            journal.change(record,dict(record,session=session))
            manager.prepare()
            scope.update(pending=None,manager=manager_receipt(manager))
            scope.update(pending="login-start")
            scope.graph=GraphicalSession(manager)
            with reserve_console(manager.network.store) as console:
                scope.graph.start(console)
                scope.update(pending=None,manager=manager_receipt(manager),graphical=graphical_receipt(scope.graph))
            return scope
        except BaseException:
            scope.release_handles()
            raise

    @classmethod
    def restore(cls,journal,store,record,*,accounts=None):
        accounts=LocalAccounts() if accounts is None else accounts
        account=accounts.by_uid(record["account"]["uid"])
        if asdict(account)!=record["account"]:raise JournalRetained("The journal session account changed")
        value=record["session"];validate_session(value,account)
        if value["pending"] not in {None,"enter"} or value["graphical"] is None:
            raise JournalRetained("An incomplete session transition requires reconciliation")
        network=restore_network(store,record,require_empty=False)
        if network is None:raise JournalRetained("The live session lost its namespace")
        dns=None;manager=None;graph=None;provider=None
        try:
            wrapped=JournalNetwork(journal,network)
            dns=JournalDNS.restore_observers(journal,wrapped,accounts=accounts)
            provider=restore_optional(journal,dns)
            manager=restore_manager(dns.files,value["manager"],netbird=provider is not None,catalog="catalog" in value)
            graph=restore_graph(manager,value["graphical"])
            from .journal_forwarding import JournalForwarding
            forwarding=JournalForwarding.restore(journal,wrapped) if "forwarding" in record else None
            scope=cls(journal,dns,manager,graph,forwarding=forwarding,provider=provider)
            scope.verify_consumers()
            if value["pending"]=="enter" or value["graphical"]["entered"]!=graph.entered:
                scope.update(pending=None,graphical=graphical_receipt(graph))
            journal.known.add(record["token"])
            return scope
        except BaseException:
            if provider is not None:provider.release_handles()
            if graph is not None and graph.pidfd is not None:graph.process.close(graph.pidfd);graph.pidfd=None
            if manager is not None:
                if manager.pidfd is not None:manager.process.close(manager.pidfd);manager.pidfd=None
                if manager.fd is not None:os.close(manager.fd);manager.fd=None
            if dns is not None:dns.release_handles()
            if network.fd is not None:os.close(network.fd);network.fd=None
            raise

    def verify_consumers(self):
        self.manager.verify();self.graph.verify();self.dns.services.verify()
        if self.forwarding is not None:self.forwarding.verify()
        if self.provider is not None:self.provider.verify()
        session=self.graph.identity.cgroup
        user=self.manager.identity.cgroup.removesuffix("/init.scope")
        service="/system.slice/"+self.graph.name
        resolver=self.dns.services.resolver.identity.pid
        provider=None if self.provider is None else self.provider.scope.unit.identity.pid
        deadline=time.monotonic()+5
        for attempt in range(3):
            try:
                values=self.network.network._ip("netns","pids",self.network.network.name).splitlines()
                for value in values:
                    if not value.isdecimal() or str(int(value))!=value or int(value)<=1:
                        raise JournalRetained("Invalid private namespace process inventory")
                    pid=int(value)
                    if pid in (resolver,provider):continue
                    fd=os.pidfd_open(pid)
                    try:
                        import select
                        if select.select([fd],[],[],0)[0]:raise ProcessLookupError()
                        start=stat_start(pid)
                        rows=Path("/proc",value,"cgroup").read_text().splitlines()
                        namespace=os.stat("/proc/"+value+"/ns/net")
                        if (len(rows)!=1 or not rows[0].startswith("0::") or not any(rows[0][3:]==p
                                or rows[0][3:].startswith(p+"/") for p in (session,user,service))
                                or (namespace.st_dev,namespace.st_ino)!=self.network.network.identity):
                            raise JournalRetained("A private namespace consumer is outside captured session scopes")
                        if stat_start(pid)!=start or select.select([fd],[],[],0)[0]:raise ProcessLookupError()
                    finally:os.close(fd)
                self.manager.verify();self.graph.verify();self.dns.services.verify()
                if self.forwarding is not None:self.forwarding.verify()
                if self.provider is not None:self.provider.verify()
                return
            except (FileNotFoundError,ProcessLookupError):
                if attempt==2 or time.monotonic()>=deadline:raise JournalRetained("Namespace process churn did not settle") from None

    def enter(self,caller,*,reauthorize=None):
        self.verify_consumers()
        self.update(pending="enter")
        result=self.graph.enter(caller,reauthorize=reauthorize)
        self.update(pending=None,graphical=graphical_receipt(self.graph))
        return result

    def close(self):
        if self.graph is not None and not self.graph.closed:
            self.update(pending="login-close")
            self.graph.close()
            self.update(pending="manager-close",graphical=None)
        self.update(pending="manager-close")
        self.manager.close()  # ManagerPending is natural logind lifetime, never user@stop.
        before=self.journal.record(self.token);updated=dict(before);updated.pop("session")
        self.journal.change(before,updated)

    def release_handles(self):
        if self.provider is not None:self.provider.release_handles()
        if self.graph is not None and self.graph.pidfd is not None:self.graph.process.close(self.graph.pidfd);self.graph.pidfd=None
        if self.manager.pidfd is not None:self.manager.process.close(self.manager.pidfd);self.manager.pidfd=None
        if self.manager.fd is not None:os.close(self.manager.fd);self.manager.fd=None
        self.dns.release_handles()
        if self.network.network.fd is not None:os.close(self.network.network.fd);self.network.network.fd=None


def recover_prepared_session(journal,store,record,*,accounts=None,sessions=None):
    """Remove a fully captured offline preparation; never adopt a partial GUI."""
    accounts=LocalAccounts() if accounts is None else accounts
    sessions=Sessions() if sessions is None else sessions
    account=accounts.by_uid(record["account"]["uid"])
    if asdict(account)!=record["account"]:raise JournalRetained("The prepared journal account changed")
    value=record["session"];validate_session(value,account)
    if (value["pending"] not in {None,"login-start"} or value["graphical"] is not None
            or value["manager"] is None or value["manager"]["live"] is not None):return False
    offline(account,accounts,sessions)
    network=restore_network(store,record,require_empty=False)
    if network is None:raise JournalRetained("The prepared session lost its namespace")
    dns=None;manager=None;provider=None
    try:
        wrapped=JournalNetwork(journal,network)
        dns,provider=restore_offline_services(journal,wrapped,accounts=accounts)
        manager=restore_manager(dns.files,value["manager"],netbird=provider is not None,catalog="catalog" in value)
        graph=GraphicalSession(manager)
        if graph.info()["LoadState"]!="not-found":
            raise JournalRetained("An unrecorded graphical unit prevents preparation recovery")
        offline(account,accounts,sessions)
        scope=JournalSession(journal,dns,manager,provider=provider)
        scope.close()  # Record close intent before touching the captured policy.
        return True
    finally:
        if provider is not None:provider.release_handles()
        if manager is not None:
            if manager.pidfd is not None:manager.process.close(manager.pidfd);manager.pidfd=None
            if manager.fd is not None:os.close(manager.fd);manager.fd=None
        if dns is not None:dns.release_handles()
        if network.fd is not None:os.close(network.fd);network.fd=None


class SessionRegistry:
    def __init__(self,journal,store):
        self.journal,self.store=journal,store
        self.profiles={}
        self.returned=set()

    def restore(self):
        from .journal_session_cleanup import recover_closed_session
        try:
            for record in self.journal.snapshot()["profiles"]:
                if "session" not in record:continue
                with self.store.locked():
                    if recover_closed_session(self.journal,self.store,record):continue
                    if recover_prepared_session(self.journal,self.store,record):continue
                    self.profiles[record["token"]]=JournalSession.restore(self.journal,self.store,record)
            recover_profiles(self.journal,self.store,exclude=self.profiles)
            return self
        except BaseException:
            self.release_handles()
            raise

    def poll(self):
        from .returning import ReturnAfterExit
        from .graphical import GraphicalPending
        from .user_manager import ManagerPending
        for token,scope in tuple(self.profiles.items()):
            if not scope.graph.closed and not scope.graph.process.exited(scope.graph.pidfd):continue
            with self.store.locked():
                if token not in self.returned:
                    result=ReturnAfterExit(self.store,scope.dns.files.accounts,scope.graph.sessions).run(scope)
                    print("karyo-profile-host: exited desktop return "+("activated" if result["changed"] else "seat-unchanged"),file=sys.stderr,flush=True)
                    self.returned.add(token)
                try:scope.close()
                except (GraphicalPending,ManagerPending):continue
                if scope.provider is not None:scope.provider.close()
                scope.dns.close()
                if scope.forwarding is not None:scope.forwarding.close()
                scope.network.close()
                scope.release_handles()
                del self.profiles[token];self.returned.discard(token)

    def release_handles(self):
        for profile in self.profiles.values():profile.release_handles()
        self.profiles.clear()
        self.returned.clear()
