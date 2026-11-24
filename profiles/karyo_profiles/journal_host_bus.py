# SPDX-License-Identifier: GPL-3.0-or-later
"""Durable ownership of the ordinary profile UID's host-service proxy."""
from dataclasses import asdict
import os

from .host_bus import HostBusScope
from .journal import JournalRetained
from .journal_dns import process_gone
from .journal_dns_record import identity, validate_unit
from .services import ServiceError, UnitIdentity


class HostBusPending(ServiceError):
    """The same acknowledged proxy unit has not completed its ordinary stop."""


def validate_host_bus(value):
    if not isinstance(value, dict) or set(value) != {'pending', 'unit', 'socket'}:
        raise ValueError('Invalid profile host-bus receipt')
    if value['pending'] not in {None, 'start', 'stop'}:raise ValueError('Invalid host-bus transition')
    if value['unit'] is None or value['socket'] is None:
        if value != {'pending':'start', 'unit':None, 'socket':None}:
            raise ValueError('Missing profile host-bus acknowledgement')
        return
    if value['pending'] == 'start':raise ValueError('Ambiguous host-bus start acknowledgement')
    validate_unit(value['unit'], 'bus');identity(value['socket'])


def unit_receipt(scope):
    scope.verify()
    unit = scope.unit
    return dict(invocation=unit.identity.invocation, pid=unit.identity.pid, start=unit.identity.start,
                exe=list(unit.executable), namespace=list(scope.network.identity),
                runtime=[list(scope.runtime_identity)])


class JournalHostBus:
    def __init__(self, journal, dns, scope):
        self.journal, self.dns, self.scope, self.token = journal, dns, scope, dns.token

    def update(self, **values):
        before = self.journal.record(self.token)
        self.journal.change(before, dict(before, host_bus=dict(before['host_bus'], **values)))

    @classmethod
    def start(cls, journal, dns):
        before = journal.record(dns.token)
        main = before.get('main_login')
        preparing_main = (main is not None and main.get('bus_view') == 'host-proxy'
                          and main['pending'] == 'entry-start' and main['login'] is None)
        child = before.get('session')
        preparing_child = (child is not None and child.get('bus_view') == 'host-proxy'
                           and child['pending'] == 'login-start' and child['graphical'] is None)
        if ('host_bus' in before or child is not None and not preparing_child or main is not None and not preparing_main
                or before['dns']['pending'] is not None):
            raise JournalRetained('The host-service proxy starts before graphical consumers')
        owner = cls(journal, dns, HostBusScope(dns))
        journal.change(before, dict(before, host_bus={'pending':'start','unit':None,'socket':None}))
        try:
            owner.scope.start()
            owner.update(pending=None, unit=unit_receipt(owner.scope), socket=list(owner.scope.socket_identity))
            return owner
        except BaseException:
            owner.release_handles()
            raise

    @classmethod
    def restore(cls, journal, dns):
        value = journal.record(dns.token)['host_bus'];validate_host_bus(value)
        if value['pending'] == 'start':raise JournalRetained('Unacknowledged host-bus creation requires review')
        owner = cls(journal, dns, HostBusScope(dns))
        scope, proof = owner.scope, value['unit']
        scope.runtime_identity, scope.socket_identity = tuple(proof['runtime'][0]), tuple(value['socket'])
        unit = scope.unit
        unit.identity = UnitIdentity(proof['invocation'], proof['pid'], '/system.slice/' + unit.name, proof['start'])
        unit.executable, unit.attempted = tuple(proof['exe']), True
        try:
            rows = unit.info()
            gone = process_gone(proof['pid'], proof['start'])
            if not gone:
                try:unit.pidfd = unit.process.open(proof['pid'])
                except ProcessLookupError:gone = True
                else:gone = unit.process.exited(unit.pidfd)
            if gone:
                same = (rows['LoadState'] == 'loaded' and rows['Transient'] == 'yes'
                        and rows['InvocationID'] == proof['invocation']
                        and rows['MainPID'] in {'0',str(proof['pid'])}
                        and rows['ControlGroup'] in {'',unit.identity.cgroup})
                if same and (rows['ActiveState'] not in {'inactive','failed'} or rows['ControlGroup']
                             or os.path.lexists(scope.directory)):
                    raise HostBusPending('Waiting for the captured profile proxy to retire')
                if rows['LoadState'] != 'not-found' and (not same or rows['MainPID'] != '0'):
                    raise JournalRetained('The completed profile proxy unit changed')
                if os.path.lexists(scope.directory) or unit.cgroup_pids():
                    raise JournalRetained('The completed profile proxy retained runtime consumers')
                unit.closed = True;owner.release_handles()
            else:
                if unit_receipt(scope) != proof:raise JournalRetained('The captured profile proxy changed')
                unit.consumer_pids()
            return owner
        except BaseException:
            owner.release_handles()
            raise

    def verify(self):
        value = self.journal.record(self.token)['host_bus']
        if (value['pending'] is not None or unit_receipt(self.scope) != value['unit']
                or list(self.scope.socket_identity) != value['socket']):
            raise JournalRetained('The host-service proxy is not an acknowledged live worker')
        self.scope.unit.consumer_pids()

    def close(self, *, sessions=None):
        from .journal_network import offline
        from .logind import Sessions
        before = self.journal.record(self.token)
        main = before.get('main_login')
        closing_main = (main is not None and main.get('bus_view') == 'host-proxy'
                        and main['pending'] == 'manager-close'
                        and process_gone(main['origin']['pid'],main['origin']['start']))
        child = before.get('session')
        closing_child = (child is not None and child.get('bus_view') == 'host-proxy'
                         and child['pending'] == 'manager-close' and child['graphical'] is None)
        if child is not None and not closing_child or main is not None and not closing_main:
            raise JournalRetained('Graphical consumers must close before their host-service proxy')
        account = self.dns.files.accounts.by_uid(self.scope.network.spec.uid)
        if asdict(account) != before['account']:raise JournalRetained('The proxy account changed')
        workers = () if self.scope.unit.closed else (self.scope.unit,)
        offline(account,self.dns.files.accounts,Sessions() if sessions is None else sessions,workers=workers)
        if not self.scope.unit.closed and unit_receipt(self.scope) != before['host_bus']['unit']:
            raise JournalRetained('The profile proxy changed before retirement')
        self.update(pending='stop')
        self.scope.close()
        before = self.journal.record(self.token)
        after = dict(before);after.pop('host_bus');self.journal.change(before,after)
        self.release_handles()

    def release_handles(self):
        self.scope.release_handles()


def recover_host_buses(journal, store, *, accounts, sessions, command=None, exclude=()):
    from .journal_dns import JournalDNS
    from .journal_network import JournalNetwork, restore_network
    for record in journal.snapshot()['profiles']:
        if record['token'] in exclude or 'host_bus' not in record:continue
        if {'session','main_login'} & record.keys():raise JournalRetained('The host bus still has graphical consumers')
        with store.locked():
            network = restore_network(store,record,command,require_empty=False)
            if network is None:raise JournalRetained('The profile proxy lost its captured namespace')
            dns = owner = None
            try:
                dns = JournalDNS.restore_observers(journal,JournalNetwork(journal,network),accounts=accounts)
                owner = JournalHostBus.restore(journal,dns)
                owner.close(sessions=sessions)
            finally:
                if owner is not None:owner.release_handles()
                if dns is not None:dns.release_handles()
                if network.fd is not None:os.close(network.fd);network.fd=None
