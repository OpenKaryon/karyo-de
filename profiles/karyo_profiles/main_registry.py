# SPDX-License-Identifier: GPL-3.0-or-later
"""Track main PAM entries separately from child compositor launchers."""
import time

from .journal_main import MainEntry
from .user_manager import ManagerPending
from .journal_network_service import NetworkServicePending
from .journal_host_bus import HostBusPending
from .runtime_services import retire_network_service
from .worker_errors import WorkerPending


class MainRegistry:
    def __init__(self, journal, store):
        self.journal, self.store = journal, store
        self.entries = {}
        self.deferred = set()
        self.next_retry = 0

    @property
    def tokens(self):
        return set(self.entries) | self.deferred

    def restore(self):
        try:
            for record in self.journal.snapshot()["profiles"]:
                if "main_login" not in record:continue
                with self.store.locked():
                    try:self.entries[record["token"]] = MainEntry.restore(self.journal, self.store, record)
                    except (ManagerPending, NetworkServicePending, HostBusPending, WorkerPending):self.deferred.add(record["token"])
            return self
        except BaseException:
            self.release_handles()
            raise

    def poll(self):
        if self.deferred and time.monotonic() >= self.next_retry:
            self.next_retry = time.monotonic() + 5
            for token in tuple(self.deferred):
                with self.store.locked():
                    try:self.entries[token] = MainEntry.restore(self.journal, self.store, self.journal.record(token))
                    except (ManagerPending, NetworkServicePending, HostBusPending, WorkerPending):continue
                    self.deferred.remove(token)
        for token, scope in tuple(self.entries.items()):
            if scope.peer is not None and not scope.peer.process.exited(scope.peer.fd):continue
            with self.store.locked():
                try:
                    scope.close()
                    # Retire the login, optional VPN and network worker before
                    # their DNS/forwarding/namespace dependencies.
                    if scope.provider is not None:
                        scope.provider.close();scope.provider.release_handles();scope.provider = None
                    observer = scope.network_service;scope.network_service = None
                    retire_network_service(self.journal, scope.dns, observer)
                except (ManagerPending, NetworkServicePending, HostBusPending, WorkerPending):continue
                scope.dns.close()
                if scope.forwarding is not None:scope.forwarding.close()
                scope.network.close()
                scope.release_handles()
                del self.entries[token]

    def release_handles(self):
        for scope in self.entries.values():scope.release_handles()
        self.entries.clear()
        self.deferred.clear()
