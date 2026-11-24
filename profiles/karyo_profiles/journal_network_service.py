# SPDX-License-Identifier: GPL-3.0-or-later
"""Durable startup and retirement of one captured private NetworkManager.

Persistent credentials stay in the account-generation-owned state directory.
Only fixed native identities enter the journal; no caller-selected executable,
namespace, service endpoint or filesystem path is accepted by this layer.
"""
import os

from .journal import JournalRetained
from .journal_dns import process_gone, runtime_identity
from .journal_network_service_record import validate_network_service
from .journal_service_state import restore_state, state_receipt
from .network_manager import NetworkManagerScope
from .provider_state import STATE_ROOT
from .services import ServiceError, UnitIdentity
from .storage import trusted_directory


class NetworkServicePending(ServiceError):
    """The same captured worker is still completing its ordinary unit stop."""


def config_receipt(scope):
    scope.verify_config()
    return {'directory': list(scope.config_identity), 'file': list(scope.config_file_identity),
            'backend': 'netplan' if scope.netplan else 'keyfile'}


def restore_config(scope, proof, *, removing=False):
    if proof['backend'] != ('netplan' if scope.netplan else 'keyfile'):
        raise JournalRetained('The packaged network configuration backend changed')
    scope.config_identity, scope.config_file_identity = tuple(proof['directory']), tuple(proof['file'])
    try:scope.config_fd = trusted_directory(scope.config)
    except FileNotFoundError:
        if not removing:raise
        return
    info = os.fstat(scope.config_fd)
    if (info.st_dev, info.st_ino) != scope.config_identity:
        raise JournalRetained('The captured private configuration directory changed')
    if not removing or os.listdir(scope.config_fd):
        if config_receipt(scope) != proof:raise JournalRetained('The captured private configuration changed')


def unit_receipt(scope):
    scope.verify()
    unit = scope.unit
    return {'invocation': unit.identity.invocation, 'pid': unit.identity.pid, 'start': unit.identity.start,
            'exe': list(unit.executable), 'namespace': list(scope.network.identity),
            'runtime': [runtime_identity(scope.directory, 0, scope.files.plan.gid)]}


class JournalNetworkService:
    def __init__(self, journal, dns, scope):
        self.journal, self.dns, self.scope = journal, dns, scope
        self.token = dns.token

    def update(self, **values):
        before = self.journal.record(self.token)
        self.journal.change(before, dict(before, network_service=dict(before['network_service'], **values)))

    @classmethod
    def start(cls, journal, dns, *, state_root=STATE_ROOT):
        before = journal.record(dns.token)
        if ({'network_service', 'session', 'main_login', 'provider'} & before.keys()
                or before['dns']['pending'] is not None):
            raise JournalRetained('Network services start before graphical consumers and optional VPN workers')
        dns.services.verify()
        scope = NetworkManagerScope(dns, state_root=state_root)
        owner = cls(journal, dns, scope)
        value = {'kind': 'network-manager', 'pending': 'state-prepare', 'state': None, 'config': None, 'unit': None}
        journal.change(before, dict(before, network=dict(before['network'], contents='tunnel'), network_service=value))
        scope.network.contents = 'tunnel'
        try:
            scope.state.prepare()
            owner.update(pending='config-create', state=state_receipt(scope.state))
            scope.prepare_config()
            owner.update(pending='service-start', config=config_receipt(scope))
            scope.start_prepared()
            # A failed ordinary connection operation must not lose the
            # already acknowledged unit's retirement authority.
            owner.update(pending='uplink-prepare', unit=unit_receipt(scope))
            from .network_uplink import prepare_uplink
            prepare_uplink(scope)
            owner.update(pending=None)
            owner.verify()
            return owner
        except BaseException:
            scope.release_handles()
            raise

    @classmethod
    def restore(cls, journal, dns, *, state_root=STATE_ROOT):
        value = journal.record(dns.token)['network_service']
        validate_network_service(value)
        if value['pending'] not in {None, 'uplink-prepare', 'service-stop', 'config-remove'}:
            raise JournalRetained('Unacknowledged network service creation requires review')
        scope = NetworkManagerScope(dns, state_root=state_root)
        owner = cls(journal, dns, scope)
        try:
            restore_state(scope.state, value['state'])
            restore_config(scope, value['config'], removing=value['pending'] == 'config-remove')
            native, unit = value['unit'], scope.unit
            if native is None:
                if unit.info()['LoadState'] != 'not-found' or os.path.lexists(scope.directory):
                    raise JournalRetained('An unknown worker occupies a retired service identity')
                unit.closed = True
                return owner
            unit.identity = UnitIdentity(native['invocation'], native['pid'], '/system.slice/' + unit.name, native['start'])
            unit.executable, unit.attempted = tuple(native['exe']), True
            rows = unit.info()
            gone = process_gone(native['pid'], native['start'])
            if not gone:
                try:unit.pidfd = unit.process.open(native['pid'])
                except ProcessLookupError:gone = True
                else:gone = unit.process.exited(unit.pidfd)
            if gone:
                if (rows['LoadState'] == 'loaded' and rows['Transient'] == 'yes'
                        and rows['InvocationID'] == native['invocation']
                        and rows['MainPID'] in {'0', str(native['pid'])}
                        and rows['ControlGroup'] in {'', unit.identity.cgroup}
                        and (rows['ActiveState'] not in {'inactive', 'failed'} or rows['ControlGroup']
                             or os.path.lexists(scope.directory))):
                    raise NetworkServicePending('Waiting for the captured network worker cgroup to retire')
                if rows['LoadState'] != 'not-found' and (
                        rows['Transient'] != 'yes' or rows['InvocationID'] != native['invocation']
                        or rows['MainPID'] != '0' or rows['ActiveState'] not in {'inactive', 'failed'} or rows['ControlGroup']):
                    raise JournalRetained('The stopped network worker identity changed')
                if os.path.lexists(scope.directory) or unit.cgroup_pids():
                    raise JournalRetained('The stopped network worker retained descendants or runtime objects')
                unit.closed = True
                if unit.pidfd is not None:unit.process.close(unit.pidfd);unit.pidfd = None
            else:
                if unit_receipt(scope) != native:raise JournalRetained('The captured network worker changed')
                scope.unit.consumer_pids()
            return owner
        except BaseException:
            scope.release_handles()
            raise

    def verify(self):
        value = self.journal.record(self.token)['network_service']
        if (value['pending'] is not None or self.scope.unit.closed
                or value['state'] != state_receipt(self.scope.state)
                or value['config'] != config_receipt(self.scope)
                or value['unit'] != unit_receipt(self.scope)):
            raise JournalRetained('The private network service is not an acknowledged live worker')
        self.scope.unit.consumer_pids()

    def consumer_pids(self):
        self.verify()
        return self.scope.unit.consumer_pids()

    def close(self):
        before = self.journal.record(self.token)
        if {'session', 'main_login', 'provider'} & before.keys():
            raise JournalRetained('Graphical and optional VPN consumers must close before network services')
        value = before['network_service']
        if value['pending'] not in {None, 'uplink-prepare', 'service-stop', 'config-remove'}:
            raise JournalRetained('Unacknowledged network worker creation cannot be retired')
        if state_receipt(self.scope.state) != value['state']:
            raise JournalRetained('The network worker account state changed')
        if value['pending'] != 'config-remove':
            if not self.scope.unit.closed:
                if value['unit'] != unit_receipt(self.scope):raise JournalRetained('The network worker changed before stop')
                self.scope.unit.consumer_pids()
            self.update(pending='service-stop')
            self.scope.unit.close()
            if os.path.lexists(self.scope.directory):raise JournalRetained('The stopped worker retained its runtime directory')
            self.update(pending='config-remove', unit=None)
        if self.scope.unit.info()['LoadState'] != 'not-found' or os.path.lexists(self.scope.directory):
            raise JournalRetained('An unknown worker appeared before configuration retirement')
        self.scope.remove_config(partial=True)
        before = self.journal.record(self.token)
        after = dict(before);after.pop('network_service')
        self.journal.change(before, after)
        self.release_handles()

    def release_handles(self):
        self.scope.release_handles()


def recover_network_services(journal, store, *, accounts=None, sessions=None, command=None,
                             exclude=(), state_root=STATE_ROOT):
    from .credentials import LocalAccounts
    from .journal_dns import JournalDNS
    from .journal_network import JournalNetwork, offline, restore_network
    from .logind import Sessions
    from .runtime_services import worker_pids
    accounts = LocalAccounts() if accounts is None else accounts
    sessions = Sessions() if sessions is None else sessions
    for record in journal.snapshot()['profiles']:
        if record['token'] in exclude or 'network_service' not in record:continue
        if {'session', 'main_login', 'provider'} & record.keys():
            raise JournalRetained('A graphical or VPN owner still depends on the network service')
        with store.locked():
            offline(accounts.by_uid(record['account']['uid']), accounts, sessions)
            network = restore_network(store, record, command, require_empty=False)
            if network is None:raise JournalRetained('The network service lost its captured namespace')
            dns = service = None
            try:
                dns = JournalDNS.restore_observers(journal, JournalNetwork(journal, network), accounts=accounts)
                service = JournalNetworkService.restore(journal, dns, state_root=state_root)
                allowed = worker_pids(service) | {dns.services.resolver.identity.pid}
                actual = {int(pid) for pid in network._ip('netns', 'pids', network.name).splitlines()}
                if actual - allowed:raise JournalRetained('Unknown consumers prevent network service retirement')
                service.close()
            finally:
                if service is not None:service.release_handles()
                if dns is not None:dns.release_handles()
                if network.fd is not None:os.close(network.fd);network.fd = None
