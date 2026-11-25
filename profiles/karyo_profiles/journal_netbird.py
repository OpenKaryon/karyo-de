# SPDX-License-Identifier: GPL-3.0-or-later
"""Checkpoint and recover the owned pre-login provider without adopting state."""
from dataclasses import asdict
import os

from .credentials import LocalAccounts
from .journal import JournalRetained
from .journal_dns import JournalDNS, runtime_identity
from .journal_netbird_record import validate_provider, RESTART_PHASES
from .journal_network import JournalNetwork, offline, restore_network
from .logind import Sessions
from .netbird import NetBirdScope
from .services import OwnedUnit
from .provider_worker import restore_unit
from .journal_service_state import state_receipt, restore_state
from .provider_state import STATE_ROOT
from .runtime_services import restore_network_service, worker_pids


def service_receipt(scope):
    scope.verify()
    unit = scope.unit
    return {"unit": {"invocation": unit.identity.invocation, "pid": unit.identity.pid,
            "start": unit.identity.start, "exe": list(unit.executable),
            "namespace": list(scope.network.identity),
            "runtime": [runtime_identity(scope.directory, 0, scope.files.plan.gid)]},
            "socket": list(scope.socket_identity), "endpoint": list(scope.endpoint_identity)}


class JournalNetBird:
    def __init__(self, journal, dns, scope):
        self.journal, self.dns, self.scope = journal, dns, scope
        self.token = dns.token

    def update(self, **values):
        before = self.journal.record(self.token)
        self.journal.change(before, dict(before, provider=dict(before["provider"], **values)))

    @classmethod
    def start(cls, journal, dns):
        before = journal.record(dns.token)
        if ("provider" in before or "session" in before or "main_login" in before or before["dns"]["pending"] is not None
                or before.get('network_service', {}).get('pending') is not None):
            raise JournalRetained("Provider startup needs acknowledged DNS before graphical consumers")
        dns.services.verify()
        scope = NetBirdScope(dns)
        owner = cls(journal, dns, scope)
        value = {"kind": "netbird", "pending": "state-prepare", "state": None, "service": None}
        delegated = dict(before["network"], contents="tunnel")
        journal.change(before, dict(before, network=delegated, provider=value))
        scope.network.contents = "tunnel"
        try:
            scope.state.prepare()
            owner.update(pending="service-start", state=state_receipt(scope.state))
            scope.start_prepared()
            owner.update(pending=None, service=service_receipt(scope))
            return owner
        except BaseException:
            scope.release_handles()  # Keep every intent; no unknown-service sweep.
            raise

    @classmethod
    def restore(cls, journal, dns):
        before = journal.record(dns.token)
        value = before["provider"]
        if asdict(dns.files.accounts.by_uid(dns.files.network.spec.uid)) != before["account"]:
            raise JournalRetained("The captured provider account changed")
        validate_provider(value)
        if value["pending"] not in {None, 'service-stop'} | RESTART_PHASES:
            raise JournalRetained("Unacknowledged provider creation requires review")
        scope = NetBirdScope(dns)
        owner = cls(journal, dns, scope)
        try:
            restore_state(scope.state, value["state"])
            proof = value["service"]
            restore_unit(scope, proof)
            if scope.unit.closed:
                return owner
            if service_receipt(scope) != proof:
                raise JournalRetained("The captured provider service, endpoint or namespace changed")
            return owner
        except BaseException:
            scope.release_handles()
            raise

    def verify(self):
        value = self.journal.record(self.token)["provider"]
        if (value["pending"] is not None or self.scope.unit.closed or value["state"] != state_receipt(self.scope.state)
                or value["service"] != service_receipt(self.scope)):
            raise JournalRetained("The provider journal is not an acknowledged live lease")

    def restart(self):
        before = self.journal.record(self.token)
        value = before['provider']
        if value['pending'] not in {None} | RESTART_PHASES:
            raise JournalRetained('The provider is being prepared or retired')
        logins = [before[key] for key in ('session', 'main_login') if key in before]
        if not logins or any(login['pending'] is not None for login in logins):
            raise JournalRetained('Provider recovery requires an acknowledged graphical owner')
        self.scope.network.verify();self.dns.services.verify()
        if state_receipt(self.scope.state) != value['state']:
            raise JournalRetained('The provider recovery account state changed')
        if not self.scope.unit.closed:
            self.verify()
            return self
        if self.scope.unit.info()['LoadState'] != 'not-found':
            from .journal_network_service import NetworkServicePending
            raise NetworkServicePending('Waiting for the retired provider unit to be collected')
        self.update(pending='restart-start', service=None)
        network = self.scope.network
        self.scope.unit = OwnedUnit(network.store, network.spec, 'netbird', 0,
                                    network.command, network=network.identity)
        self.scope.socket_identity = self.scope.endpoint_identity = self.scope.endpoint_contents = None
        self.scope.start_prepared()
        self.update(pending=None, service=service_receipt(self.scope))
        self.verify()
        return self

    def close(self):
        before = self.journal.record(self.token)
        if "session" in before or "main_login" in before:
            raise JournalRetained("The graphical profile must close before its provider")
        value = before["provider"]
        if value["pending"] not in {None, 'service-stop'} | RESTART_PHASES or value["state"] != state_receipt(self.scope.state):
            raise JournalRetained("Provider cleanup identity changed")
        if not self.scope.unit.closed and value["service"] != service_receipt(self.scope):
            raise JournalRetained("The live provider changed before cleanup")
        if value['service'] is not None:self.update(pending='service-stop')
        self.scope.close()
        before = self.journal.record(self.token)
        updated = dict(before); updated.pop("provider")
        self.journal.change(before, updated)

    def release_handles(self):
        self.scope.release_handles()


def recover_providers(journal, store, *, accounts=None, sessions=None, command=None, exclude=(),
                      network_service_state_root=STATE_ROOT):
    accounts = LocalAccounts() if accounts is None else accounts
    sessions = Sessions() if sessions is None else sessions
    for record in journal.snapshot()["profiles"]:
        if record["token"] in exclude or "provider" not in record:
            continue
        if "session" in record or "main_login" in record:
            raise JournalRetained("The provider still has a graphical owner")
        with store.locked():
            account = accounts.by_uid(record["account"]["uid"])
            offline(account, accounts, sessions)
            network = restore_network(store, record, command, require_empty=False)
            if network is None:
                raise JournalRetained("The provider lost its captured namespace")
            dns = owner = network_service = None
            try:
                wrapped = JournalNetwork(journal, network)
                dns = JournalDNS.restore_observers(journal, wrapped, accounts=accounts)
                network_service = restore_network_service(journal, dns, state_root=network_service_state_root)
                owner = JournalNetBird.restore(journal, dns)
                allowed = {str(pid) for pid in worker_pids(owner, network_service)} | {str(dns.services.resolver.identity.pid)}
                consumers = network._ip("netns", "pids", network.name).splitlines()
                if set(consumers) - allowed:
                    raise JournalRetained("Unknown consumers prevent offline provider cleanup")
                owner.close()
            finally:
                if owner is not None: owner.release_handles()
                if network_service is not None:network_service.release_handles()
                if dns is not None: dns.release_handles()
                if network.fd is not None: os.close(network.fd); network.fd = None


def restore_optional(journal, dns):
    if "provider" not in journal.record(dns.token):
        return None
    provider = JournalNetBird.restore(journal, dns)
    try:
        if not provider.scope.unit.closed:provider.verify()
        return provider
    except BaseException:
        provider.release_handles()
        raise

def refresh_optional(journal, dns, observer):
    if observer is None:return None
    unit = observer.scope.unit
    pending = journal.record(dns.token)['provider']['pending']
    if (not unit.closed and unit.pidfd is not None and not unit.process.exited(unit.pidfd)
            and pending not in RESTART_PHASES):return observer
    observer.release_handles()
    current = restore_optional(journal, dns)
    if current is None:return None
    try:
        current.restart()
        return current
    except BaseException:
        current.release_handles()
        raise


def restore_offline_services(journal, network, *, accounts=None):
    """Observe a captured provider alongside DNS; never stop a graphical owner."""
    record = journal.record(network.token)
    if not {'provider', 'network_service', 'host_bus'} & record.keys():
        return JournalDNS.restore(journal, network, accounts=accounts), None, None
    dns = JournalDNS.restore_observers(journal, network, accounts=accounts)
    provider = network_service = host_bus = None
    try:
        provider = restore_optional(journal, dns)
        network_service = restore_network_service(journal, dns)
        if 'host_bus' in record:
            from .journal_host_bus import JournalHostBus
            host_bus=JournalHostBus.restore(journal,dns)
        allowed = {str(pid) for pid in worker_pids(provider, network_service, host_bus)} | {str(dns.services.resolver.identity.pid)}
        values = network.network._ip("netns", "pids", network.network.name).splitlines()
        if set(values) - allowed:
            raise JournalRetained("Unknown consumers prevent offline profile reconciliation")
        dns.services.verify()
        if provider is not None and not provider.scope.unit.closed:provider.verify()
        return dns, provider, network_service
    except BaseException:
        if provider is not None: provider.release_handles()
        if network_service is not None:network_service.release_handles()
        dns.release_handles()
        raise
    finally:
        if host_bus is not None:host_bus.release_handles()
