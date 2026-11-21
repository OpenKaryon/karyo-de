# SPDX-License-Identifier: GPL-3.0-or-later
"""Checkpoint and recover the owned pre-login provider without adopting state."""
from dataclasses import asdict
import os

from .credentials import LocalAccounts
from .journal import JournalRetained
from .journal_dns import JournalDNS, process_gone, runtime_identity
from .journal_netbird_record import validate_provider
from .journal_network import JournalNetwork, offline, restore_network
from .logind import Sessions
from .netbird import NetBirdScope
from .services import UnitIdentity
from .storage import trusted_directory


def state_receipt(state):
    state.verify()
    info = os.stat("owner.json", dir_fd=state.fd, follow_symlinks=False)
    return {"directory": list(state.identity), "stamp": [info.st_dev, info.st_ino],
            "home": list(state.home_identity)}


def service_receipt(scope):
    scope.verify()
    unit = scope.unit
    return {"unit": {"invocation": unit.identity.invocation, "pid": unit.identity.pid,
            "start": unit.identity.start, "exe": list(unit.executable),
            "namespace": list(scope.network.identity),
            "runtime": [runtime_identity(scope.directory, 0, scope.files.plan.gid)]},
            "socket": list(scope.socket_identity), "endpoint": list(scope.endpoint_identity)}


def restore_state(state, proof):
    state.home_identity = tuple(proof["home"])
    state.owner = state.owner_contents(state.home_identity)
    try:
        state.fd = trusted_directory(state.path)
        state.identity = tuple(proof["directory"])
        if state_receipt(state) != proof:
            raise JournalRetained("The captured provider state or ownership stamp changed")
    except BaseException:
        state.release_handles()
        raise


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
        if ("provider" in before or "session" in before or before["dns"]["pending"] is not None):
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
        if value["pending"] not in {None, "service-stop"} or value["service"] is None:
            raise JournalRetained("Unacknowledged provider creation requires review")
        scope = NetBirdScope(dns)
        owner = cls(journal, dns, scope)
        try:
            restore_state(scope.state, value["state"])
            proof = value["service"]
            unit = scope.unit
            native = proof["unit"]
            unit.identity = UnitIdentity(native["invocation"], native["pid"], "/system.slice/" + unit.name, native["start"])
            unit.executable = tuple(native["exe"])
            unit.attempted = True
            scope.socket_identity = tuple(proof["socket"])
            scope.endpoint_identity = tuple(proof["endpoint"])
            rows = unit.info()
            if value["pending"] == "service-stop" and process_gone(native["pid"], native["start"]):
                if rows["LoadState"] != "not-found" and (
                        rows["Transient"] != "yes" or rows["InvocationID"] != native["invocation"]
                        or rows["MainPID"] != "0" or rows["ActiveState"] not in {"inactive", "failed"}
                        or rows["ControlGroup"]):
                    raise JournalRetained("Stopped provider unit identity changed")
                if scope.directory.exists():
                    raise JournalRetained("Stopped provider retained runtime objects")
                unit.closed = True
                return owner
            unit.pidfd = unit.process.open(native["pid"])
            scope.endpoint_contents = scope.endpoint_text()
            if service_receipt(scope) != proof:
                raise JournalRetained("The captured provider service, endpoint or namespace changed")
            return owner
        except BaseException:
            scope.release_handles()
            raise

    def verify(self):
        value = self.journal.record(self.token)["provider"]
        if (value["pending"] is not None or value["state"] != state_receipt(self.scope.state)
                or value["service"] != service_receipt(self.scope)):
            raise JournalRetained("The provider journal is not an acknowledged live lease")

    def close(self):
        before = self.journal.record(self.token)
        if "session" in before:
            raise JournalRetained("The graphical profile must close before its provider")
        value = before["provider"]
        if value["pending"] not in {None, "service-stop"} or value["state"] != state_receipt(self.scope.state):
            raise JournalRetained("Provider cleanup identity changed")
        if not self.scope.unit.closed and value["service"] != service_receipt(self.scope):
            raise JournalRetained("The live provider changed before cleanup")
        self.update(pending="service-stop")
        self.scope.close()
        before = self.journal.record(self.token)
        updated = dict(before); updated.pop("provider")
        self.journal.change(before, updated)

    def release_handles(self):
        self.scope.release_handles()


def recover_providers(journal, store, *, accounts=None, sessions=None, command=None, exclude=()):
    accounts = LocalAccounts() if accounts is None else accounts
    sessions = Sessions() if sessions is None else sessions
    for record in journal.snapshot()["profiles"]:
        if record["token"] in exclude or "provider" not in record:
            continue
        if "session" in record:
            raise JournalRetained("The provider still has a graphical owner")
        with store.locked():
            account = accounts.by_uid(record["account"]["uid"])
            offline(account, accounts, sessions)
            network = restore_network(store, record, command, require_empty=False)
            if network is None:
                raise JournalRetained("The provider lost its captured namespace")
            dns = owner = None
            try:
                wrapped = JournalNetwork(journal, network)
                dns = JournalDNS.restore_observers(journal, wrapped, accounts=accounts)
                owner = JournalNetBird.restore(journal, dns)
                allowed = {str(dns.services.resolver.identity.pid)}
                if not owner.scope.unit.closed:
                    allowed.add(str(owner.scope.unit.identity.pid))
                consumers = network._ip("netns", "pids", network.name).splitlines()
                if set(consumers) - allowed:
                    raise JournalRetained("Unknown consumers prevent offline provider cleanup")
                owner.close()
            finally:
                if owner is not None: owner.release_handles()
                if dns is not None: dns.release_handles()
                if network.fd is not None: os.close(network.fd); network.fd = None


def restore_optional(journal, dns):
    if "provider" not in journal.record(dns.token):
        return None
    provider = JournalNetBird.restore(journal, dns)
    try:
        provider.verify()
        return provider
    except BaseException:
        provider.release_handles()
        raise


def restore_offline_services(journal, network, *, accounts=None):
    """Observe a captured provider alongside DNS; never stop a graphical owner."""
    if "provider" not in journal.record(network.token):
        return JournalDNS.restore(journal, network, accounts=accounts), None
    dns = JournalDNS.restore_observers(journal, network, accounts=accounts)
    provider = None
    try:
        provider = restore_optional(journal, dns)
        allowed = {str(dns.services.resolver.identity.pid), str(provider.scope.unit.identity.pid)}
        values = network.network._ip("netns", "pids", network.network.name).splitlines()
        if set(values) - allowed:
            raise JournalRetained("Unknown consumers prevent offline profile reconciliation")
        dns.services.verify(); provider.verify()
        return dns, provider
    except BaseException:
        if provider is not None: provider.release_handles()
        dns.release_handles()
        raise
