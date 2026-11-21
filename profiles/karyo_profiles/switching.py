# SPDX-License-Identifier: GPL-3.0-or-later
"""Switch existing family sessions only after required compositor locks confirm."""
import time

from .credentials import LocalAccounts
from .logind import SessionError, Sessions
from .model import account_name


class DesktopStopped(SessionError):
    """A fresh session needs the separate controlled startup path."""


class ExistingSwitcher:
    def __init__(self, store, accounts=None, sessions=None, *, clock=time.monotonic,
                 sleep=time.sleep, today=lambda: int(time.time() // 86400)):
        self.store = store
        self.accounts = accounts if accounts is not None else LocalAccounts()
        self.sessions = sessions if sessions is not None else Sessions()
        self.clock = clock
        self.sleep = sleep
        self.today = today

    @staticmethod
    def identity(value):
        return value.id, value.name, value.uid, value.vt

    def remaining(self, deadline):
        remaining = deadline - self.clock()
        if remaining <= 0:
            raise TimeoutError("Desktop lock readiness deadline expired")
        return min(2, remaining)

    def read_same(self, value, deadline):
        current = self.sessions.get(value.id, timeout=self.remaining(deadline))
        if current is None or self.identity(current) != self.identity(value):
            raise SessionError("The selected desktop session changed during switching")
        return current

    def lock_ready(self, value, deadline):
        self.sessions.lock(value.id, timeout=self.remaining(deadline))
        while True:
            current = self.read_same(value, deadline)
            if current.active != value.active:
                raise SessionError("The active desktop changed during lock preparation")
            if current.locked:
                return
            self.sleep(min(0.05, max(0, deadline - self.clock())))

    @staticmethod
    def single(values, account, *, active=False):
        found = [value for value in values if value.name == account.name and value.uid == account.uid]
        if active:
            found = [value for value in found if value.active]
        if not found:
            raise DesktopStopped("The selected desktop has no running graphical session")
        if len(found) != 1:
            raise SessionError("The selected desktop has ambiguous graphical sessions")
        return found[0]

    def switch(self, caller, target):
        account_name(target)
        # Share provisioning/password synchronization's lock: expired pending
        # accounts and half-published configuration must never become a target.
        with self.store.locked():
            if self.accounts.by_uid(caller.uid) != caller:
                raise PermissionError("The authenticated desktop account changed")
            family = self.store.ownership().family(caller.name)
            if target not in family:
                raise PermissionError("Only desktops in the caller's family may be selected")
            selected = self.accounts.account(target)
            source_policy = self.store.settings(caller.name)
            target_policy = self.store.settings(target)
            expiry = self.accounts.expiry_day(target)
            password_hash = self.accounts.password_hash(target)
            if ((expiry is not None and expiry <= self.today()) or not password_hash
                    or password_hash.startswith(("!", "*"))):
                raise PermissionError("The selected desktop account is locked or expired")
            values = self.sessions.all()
            source = self.single(values, caller, active=True)
            destination = self.single(values, selected)
            if self.identity(source) == self.identity(destination):
                return {"user": selected.name, "vt": destination.vt, "changed": False}
            if destination.active or destination.vt == source.vt:
                raise SessionError("Another desktop unexpectedly owns the active seat")
            deadline = self.clock() + 8
            protected_target = target_policy.password != "none"
            protected_source = source_policy.password != "none"
            # An inactive protected target must confirm an actual lock before
            # it is shown. A protected source is also locked before we leave it.
            if protected_target:
                self.lock_ready(destination, deadline)
            if protected_source:
                self.lock_ready(source, deadline)
            current = self.read_same(source, deadline)
            wanted = self.read_same(destination, deadline)
            if (not current.active or wanted.active or (protected_target and not wanted.locked)
                    or (protected_source and not current.locked)):
                raise SessionError("Desktop lock readiness changed before activation")
            self.sessions.activate(destination.id, timeout=self.remaining(deadline))
            # A successful method call is not proof the VT actually changed.
            while True:
                wanted = self.read_same(destination, deadline)
                if wanted.active:
                    return {"user": selected.name, "vt": destination.vt, "changed": True}
                self.sleep(min(0.05, max(0, deadline - self.clock())))
