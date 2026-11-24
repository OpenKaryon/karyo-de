# SPDX-License-Identifier: GPL-3.0-or-later
"""Durable ownership of an early main PAM entry and its native user manager.

This module does not enable PAM or authorize authentication. The host transport
must select the account and capture MainPeer before calling prepare. A close
notification records intent; only natural parent/manager exit permits cleanup.
"""
from dataclasses import asdict
import os
from pathlib import Path

from .credentials import LocalAccounts
from .journal import JournalRetained
from .journal_dns import JournalDNS, process_gone
from .journal_main_record import validate_main
from .journal_network import JournalNetwork, restore_network
from .journal_session import manager_receipt, restore_manager, verify_namespace_consumers
from .journal_session_record import owner
from .journal_session_cleanup import removing_manager
from .logind import Sessions, process_session
from .main_peer import MainPeer, object_identity
from .storage import trusted_directory
from .user_manager import ManagerPending, UserManagerScope
from .runtime_services import refresh_network_service, restore_network_service


class MainEntry:
    is_main = True
    def __init__(self, journal, dns, manager, peer=None):
        self.journal, self.dns, self.manager, self.peer = journal, dns, manager, peer
        self.network, self.token = dns.network, dns.token
        self.closed = False
        record = journal.record(self.token)
        from .journal_forwarding import JournalForwarding
        from .journal_netbird import restore_optional
        self.forwarding = JournalForwarding.restore(journal, self.network) if "forwarding" in record else None
        self.provider = restore_optional(journal, dns)
        try:self.network_service = restore_network_service(journal, dns)
        except BaseException:
            if self.provider is not None:self.provider.release_handles()
            raise

    def update(self, **values):
        before = self.journal.record(self.token)
        self.journal.change(before, dict(before, main_login=dict(before["main_login"], **values)))

    @classmethod
    def prepare(cls, journal, dns, peer):
        before = journal.record(dns.token)
        if (before.get("entry") != "main" or {"main_login", "session"} & before.keys()
                or before["dns"]["pending"] is not None):
            raise JournalRetained("Main entry requires its acknowledged offline network and DNS")
        peer.verify(cgroup=peer.origin["cgroup"], network=object_identity("/proc/1/ns/net"))
        dns.services.verify()
        manager = UserManagerScope(dns.files, netbird="provider" in before)
        scope = cls(journal, dns, manager, peer)
        journal.change(before, dict(before, main_login={"pending": "manager-prepare", "origin": peer.origin,
                                                      "manager": None, "login": None}))
        # Intent precedes filesystem mutation; the acknowledged inode receipt
        # precedes handing either context FD to the PAM client.
        try:
            manager.prepare()
            scope.update(pending="entry-start", manager=manager_receipt(manager))
            return scope
        except BaseException:
            scope.release_handles()
            raise

    def descriptors(self):
        value = self.journal.record(self.token)["main_login"]
        if value["pending"] != "entry-start" or self.peer is None:
            raise JournalRetained("The main context is not awaiting its PAM entry")
        self.peer.verify(cgroup=value["origin"]["cgroup"], network=object_identity("/proc/1/ns/net"))
        self.dns.files.verify()
        path = Path(self.dns.files.plan.runtime_paths[0])
        parent = trusted_directory(path.parent)
        try:
            resolver = os.open(path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
        finally:
            os.close(parent)
        try:
            info = os.fstat(resolver)
            if [info.st_dev, info.st_ino] != self.journal.record(self.token)["dns"]["resolver"]["runtime"][0]:
                raise JournalRetained("The captured main resolver directory changed")
            return os.dup(self.network.network.fd), resolver
        except BaseException:
            os.close(resolver)
            raise

    def login(self):
        if self.peer is None:
            raise JournalRetained("The main PAM parent is no longer available")
        account = self.manager.account
        value = self.journal.record(self.token)["main_login"]
        pid = value["origin"]["pid"]
        login = self.manager.sessions.owner(process_session(pid))
        current = self.manager.sessions.get(login.id)
        if (login.uid != account.uid or login.name != account.name or login.kind != "user"
                or login.leader != pid or login.service != value["origin"]["service"]
                or login.state not in {"active", "online"} or current is None
                or current.uid != account.uid or current.name != account.name):
            raise JournalRetained("The main PAM login belongs to another session")
        cgroup = "/user.slice/user-" + str(account.uid) + ".slice/" + login.scope
        self.peer.verify(cgroup=cgroup, network=self.network.network.identity)
        if object_identity("/proc/" + str(pid) + "/root/run/systemd/resolve") != object_identity(self.dns.files.plan.runtime_paths[0]):
            raise JournalRetained("The main PAM resolver mount changed")
        return login

    def attach(self):
        value = self.journal.record(self.token)["main_login"]
        if value["pending"] != "entry-start" or value["login"] is not None:
            raise JournalRetained("The main PAM login was already attached or cancelled")
        login = self.login()
        self.manager.capture(login)
        self.update(pending=None, manager=manager_receipt(self.manager), login=asdict(login))
        self.verify_consumers()

    def verify(self):
        value = self.journal.record(self.token)["main_login"]
        if (value["pending"] is not None
                or self.login().identity != owner(value["login"], self.manager.account,
                     login_services=(value["origin"]["service"],)).identity):
            raise JournalRetained("The main PAM login is not an acknowledged live entry")
        self.manager.verify()
        self.dns.services.verify()
        if self.forwarding is not None:self.forwarding.verify()
        if self.provider is not None:self.provider.verify()
        self.network_service = refresh_network_service(self.journal, self.dns, self.network_service)
        if self.network_service is not None and not self.network_service.scope.unit.closed:
            self.network_service.verify()

    def verify_consumers(self):
        login = self.login()
        roots = ("/user.slice/user-" + str(self.manager.account.uid) + ".slice/" + login.scope,
                 self.manager.identity.cgroup.removesuffix("/init.scope"))
        verify_namespace_consumers(self, roots, self.verify)

    def close_hint(self):
        value = self.journal.record(self.token)["main_login"]
        if value["pending"] not in {None, "entry-start", "login-close"}:
            raise JournalRetained("The main PAM entry is already being retired")
        self.peer.verify()
        self.update(pending="login-close")

    def close(self):
        if self.closed:return
        value = self.journal.record(self.token)["main_login"]
        origin = value["origin"]
        if not process_gone(origin["pid"], origin["start"]):
            raise ManagerPending("Waiting for the original main PAM parent to exit")
        # A failed PAM stack may start user@UID without reaching attach. Do not
        # adopt that manager and do not stop it to force namespace cleanup.
        if self.manager.identity is None and not self.manager.inactive(self.manager.info()):
            raise ManagerPending("Waiting for an incomplete main login to finish naturally")
        self.update(pending="manager-close")
        self.manager.close()
        before = self.journal.record(self.token)
        updated = dict(before); updated.pop("main_login")
        self.journal.change(before, updated)
        if self.peer is not None:self.peer.close();self.peer = None
        self.closed = True

    @classmethod
    def restore(cls, journal, store, record, *, accounts=None):
        accounts = LocalAccounts() if accounts is None else accounts
        account = accounts.by_uid(record["account"]["uid"])
        if asdict(account) != record["account"]:
            raise JournalRetained("The captured main account changed")
        value = record["main_login"]
        validate_main(value, account)
        if value["manager"] is None:
            raise JournalRetained("Unacknowledged main manager preparation requires review")
        network = restore_network(store, record, require_empty=False)
        if network is None:raise JournalRetained("The main entry lost its captured namespace")
        dns = manager = peer = scope = None
        try:
            dns = JournalDNS.restore_observers(journal, JournalNetwork(journal, network), accounts=accounts)
            origin = value["origin"]
            gone = process_gone(origin["pid"], origin["start"])
            if gone:
                probe = UserManagerScope(dns.files, netbird="provider" in record)
                if not probe.inactive(probe.info()):
                    raise ManagerPending("Waiting for the recorded main manager to finish before recovery")
                manager = removing_manager(dns.files, value["manager"],
                                          allow_missing=value["pending"] == "manager-close", netbird="provider" in record)
            else:
                peer = MainPeer(origin)
                manager = restore_manager(dns.files, value["manager"], netbird="provider" in record,
                                          login_services=(origin["service"],), allow_starting=True)
            scope = cls(journal, dns, manager, peer)
            if not gone and value["pending"] is None:scope.verify_consumers()
            journal.known.add(record["token"])
            return scope
        except BaseException:
            if scope is not None:
                scope.release_handles()
            else:
                if peer is not None:peer.close()
                if manager is not None:
                    if manager.pidfd is not None:manager.process.close(manager.pidfd)
                    if manager.fd is not None:os.close(manager.fd)
                if dns is not None:dns.release_handles()
                if network.fd is not None:os.close(network.fd);network.fd = None
            raise

    def release_handles(self):
        if self.provider is not None:self.provider.release_handles()
        if self.network_service is not None:self.network_service.release_handles()
        if self.peer is not None:self.peer.close();self.peer = None
        if self.manager.pidfd is not None:self.manager.process.close(self.manager.pidfd);self.manager.pidfd = None
        if self.manager.fd is not None:os.close(self.manager.fd);self.manager.fd = None
        self.dns.release_handles()
        if self.network.network.fd is not None:os.close(self.network.network.fd);self.network.network.fd = None
