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
from .logind import LoginOwner, Sessions, process_session
from .main_peer import MainPeer, object_identity
from .services import host_controller, program_identity
from .storage import trusted_directory
from .user_manager import ManagerPending, UserManagerScope
from .runtime_services import refresh_network_service, restore_network_service
from .host_network_policy import verify_host_network_policy,retire_host_network_policy


def finish_main_session(value, sessions):
    """Signal only the captured closing logind session after its PAM parent exits."""
    host_controller()
    if value['login'] is None:return
    origin=value['origin']
    if not process_gone(origin['pid'],origin['start']):return
    captured=LoginOwner(**value['login'])
    owners=[current for current in sessions.owners(captured.uid) if current.id==captured.id]
    if not owners:return
    if len(owners)!=1 or owners[0].identity!=captured.identity:
        raise JournalRetained('The closing main logind session identity changed')
    if value['pending'] not in {'login-close','manager-close'}:
        raise JournalRetained('The main session has no recorded close intent')
    if owners[0].state=='closing':
        # TerminateSession is a no-op after logind has abandoned a closing
        # session. Its KillSession operation still targets that exact login,
        # without stopping user@UID or sessions belonging to another desktop.
        sessions.command.run('kill-session','--signal=TERM',captured.id,timeout=5)


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
        self.network_service = self.host_bus = None
        try:
            self.network_service = restore_network_service(journal, dns)
            if 'host_bus' in record:
                from .journal_host_bus import JournalHostBus
                self.host_bus = JournalHostBus.restore(journal,dns)
        except BaseException:
            if self.provider is not None:self.provider.release_handles()
            if self.network_service is not None:self.network_service.release_handles()
            raise

    def update(self, **values):
        before = self.journal.record(self.token)
        self.journal.change(before, dict(before, main_login=dict(before["main_login"], **values)))

    @classmethod
    def prepare(cls, journal, dns, peer, *, host_bus=False):
        before = journal.record(dns.token)
        if (before.get("entry") != "main" or {"main_login", "session"} & before.keys()
                or before["dns"]["pending"] is not None):
            raise JournalRetained("Main entry requires its acknowledged offline network and DNS")
        peer.verify(cgroup=peer.origin["cgroup"], network=object_identity("/proc/1/ns/net"))
        dns.services.verify()
        verify_host_network_policy(journal,dns.network.network,accounts=dns.files.accounts,sessions=Sessions())
        if type(host_bus) is not bool:raise ValueError('The main host-bus view must be explicit')
        if host_bus:program_identity('/usr/bin/xdg-dbus-proxy')
        network_client=before.get('network_service',{}).get('clients',False)
        manager = UserManagerScope(dns.files, netbird="provider" in before,host_bus=host_bus,network_client=network_client)
        scope = cls(journal, dns, manager, peer)
        login = {'pending':'manager-prepare','origin':peer.origin,'manager':None,'login':None}
        if host_bus:login['bus_view'] = 'host-proxy'
        if network_client:login['network_client']=True
        journal.change(before, dict(before, main_login=login))
        # Intent precedes filesystem mutation; the acknowledged inode receipt
        # precedes handing either context FD to the PAM client.
        try:
            manager.prepare()
            scope.update(pending="entry-start", manager=manager_receipt(manager))
            if host_bus:
                from .journal_host_bus import JournalHostBus
                scope.host_bus = JournalHostBus.start(journal,dns)
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
        verify_host_network_policy(self.journal,self.network.network,
            accounts=self.dns.files.accounts,sessions=self.manager.sessions)
        path = Path(self.dns.files.plan.runtime_paths[0])
        parent = trusted_directory(path.parent)
        try:
            resolver = os.open(path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
        finally:
            os.close(parent)
        descriptors = [resolver]
        try:
            info = os.fstat(resolver)
            if [info.st_dev, info.st_ino] != self.journal.record(self.token)["dns"]["resolver"]["runtime"][0]:
                raise JournalRetained("The captured main resolver directory changed")
            descriptors.insert(0,os.dup(self.network.network.fd))
            if value.get('bus_view') == 'host-proxy':
                if self.host_bus is None:raise JournalRetained('The main entry lost its host-service proxy')
                self.host_bus.verify()
                descriptors.append(self.host_bus.scope.descriptor())
            return tuple(descriptors)
        except BaseException:
            for descriptor in descriptors:os.close(descriptor)
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
        if value.get('bus_view') == 'host-proxy':
            if self.host_bus is None or object_identity('/proc/' + str(pid) + '/root/run/dbus/system_bus_socket') != self.host_bus.scope.socket_identity:
                raise JournalRetained('The main PAM host-service socket mount changed')
        return login

    def attach(self):
        value = self.journal.record(self.token)["main_login"]
        if value["pending"] != "entry-start" or value["login"] is not None:
            raise JournalRetained("The main PAM login was already attached or cancelled")
        login = self.login()
        self.manager.capture(login)
        self.update(pending=None, manager=manager_receipt(self.manager), login=asdict(login))
        self.verify_consumers()

    def verify_context(self):
        value = self.journal.record(self.token)["main_login"]
        if (value["pending"] is not None
                or self.login().identity != owner(value["login"], self.manager.account,
                     login_services=(value["origin"]["service"],)).identity):
            raise JournalRetained("The main PAM login is not an acknowledged live entry")
        self.manager.verify()
        self.dns.services.verify()
        verify_host_network_policy(self.journal,self.network.network,
            accounts=self.dns.files.accounts,sessions=self.manager.sessions)
        if self.host_bus is not None:self.host_bus.verify()

    def verify(self):
        self.verify_context()
        if self.forwarding is not None:self.forwarding.verify()
        self.network_service = refresh_network_service(self.journal, self.dns, self.network_service)
        if self.network_service is not None and not self.network_service.scope.unit.closed:
            self.network_service.verify()
        from .journal_netbird import refresh_optional
        self.provider = refresh_optional(self.journal, self.dns, self.provider)
        if self.provider is not None:self.provider.verify()

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
        record=self.journal.record(self.token)
        if 'main_login' not in record:
            retire_host_network_policy(self.journal,self.network.network,
                accounts=self.dns.files.accounts,sessions=self.manager.sessions)
            if self.peer is not None:self.peer.close();self.peer=None
            self.closed=True
            return
        value = record["main_login"]
        origin = value["origin"]
        if not process_gone(origin["pid"], origin["start"]):
            raise ManagerPending("Waiting for the original main PAM parent to exit")
        # A failed PAM stack may start user@UID without reaching attach. Do not
        # adopt that manager and do not stop it to force namespace cleanup.
        if self.manager.identity is None and not self.manager.inactive(self.manager.info()):
            raise ManagerPending("Waiting for an incomplete main login to finish naturally")
        self.update(pending="manager-close")
        finish_main_session(self.journal.record(self.token)['main_login'],self.manager.sessions)
        if self.host_bus is not None:
            if not self.manager.inactive(self.manager.info()) or self.manager.sessions.owners(self.manager.account.uid):
                raise ManagerPending('Waiting for the captured main user manager before proxy retirement')
            from .journal_host_bus import JournalHostBus
            self.host_bus.release_handles()
            self.host_bus = JournalHostBus.restore(self.journal,self.dns)
            self.host_bus.close(sessions=self.manager.sessions)
            self.host_bus.release_handles();self.host_bus = None
        self.manager.close()
        before = self.journal.record(self.token)
        updated = dict(before); updated.pop("main_login")
        self.journal.change(before, updated)
        retire_host_network_policy(self.journal,self.network.network,
            accounts=self.dns.files.accounts,sessions=self.manager.sessions)
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
                if value['pending'] not in {'login-close','manager-close'}:
                    closing=dict(record,main_login=dict(value,pending='login-close'))
                    journal.change(record,closing);record=closing;value=closing['main_login']
                finish_main_session(value,Sessions())
                probe = UserManagerScope(dns.files, netbird="provider" in record)
                if not probe.inactive(probe.info()):
                    raise ManagerPending("Waiting for the recorded main manager to finish before recovery")
                manager = removing_manager(dns.files, value["manager"],
                                          allow_missing=value["pending"] == "manager-close", netbird="provider" in record,
                                          host_bus='bus_view' in value,network_client=value.get('network_client',False))
            else:
                peer = MainPeer(origin)
                manager = restore_manager(dns.files, value["manager"], netbird="provider" in record,
                                          login_services=(origin["service"],), allow_starting=True,host_bus='bus_view' in value,
                                          network_client=value.get('network_client',False))
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
        if self.host_bus is not None:self.host_bus.release_handles()
        if self.peer is not None:self.peer.close();self.peer = None
        if self.manager.pidfd is not None:self.manager.process.close(self.manager.pidfd);self.manager.pidfd = None
        if self.manager.fd is not None:os.close(self.manager.fd);self.manager.fd = None
        self.dns.release_handles()
        if self.network.network.fd is not None:os.close(self.network.network.fd);self.network.network.fd = None
