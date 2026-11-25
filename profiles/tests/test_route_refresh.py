# SPDX-License-Identifier: GPL-3.0-or-later
"""Carrier changes use captured native nft JSON and ordinary route lifecycles."""
from copy import deepcopy
from dataclasses import replace
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.dns import DNSSeed, ResolverPlan
from karyo_profiles.dns_refresh import encoded
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.networks import NetworkError, NetworkSpec
from karyo_profiles.route_refresh import refresh_routes,lost_physical_routes
from test_dns_refresh import Journal as DNSJournal, Link
from test_forwarding import Kernel as BaseKernel, PHYSICAL
from test_journal_forwarding import Journal, base_record

NATIVE = json.loads((Path(__file__).parent/'fixtures/forwarding_native.json').read_bytes())['snapshots']
SPEC = NetworkSpec(1002, 'ab' * 16)
OLD = DNSSeed(PHYSICAL, ('192.0.2.1',), ('old.example',))
NEW = DNSSeed(replace(PHYSICAL, interface='eth1', index=3,
    address='198.51.100.2', gateway='198.51.100.1'), ('198.51.100.53',), ('new.example',))
THIRD = DNSSeed(replace(PHYSICAL, interface='eth2', index=4,
    address='203.0.113.2', gateway='203.0.113.1'), ('203.0.113.53',), ('third.example',))


class Kernel(BaseKernel):
    def __init__(self):
        super().__init__(SPEC)
        self.mode = None
        self.route_phases = []
        self.fail_default_once = False

    def run(self, *args, input_data=None):
        if args[0] == '/usr/sbin/nft':
            self.calls.append((args, input_data))
            if args[1:] == ('-j', 'list', 'tables'):
                return json.dumps({'nftables': [] if self.firewall is None else [self.firewall[0]]})
            if args[1:4] == ('-j', 'list', 'table'):
                return json.dumps({'nftables': self.firewall})
            if args[1:] == ('-f', '-'):
                if input_data.startswith(b'create table'):
                    self.mode = 'original'
                elif b'drop comment' in input_data:
                    self.mode = 'paused'
                else:
                    self.mode = next(device for device in ('eth0', 'eth1', 'eth2')
                                     if ('"' + device + '"').encode() in input_data)
                self.firewall = deepcopy(NATIVE[self.mode])
            elif args[1:4] == ('delete', 'table', 'inet'):
                assert args[-1] == str(self.firewall[0]['table']['handle'])
                self.firewall = None
            return ''
        if args[2:4] in (('route', 'replace'), ('route', 'delete')):
            self.route_phases.append(self.mode)
            if args[3] == 'replace':
                if args[4] == 'default' and self.fail_default_once:
                    self.fail_default_once = False
                    raise TimeoutError('The ordinary route command did not complete')
                # Let the independent existing fixture parse native route args.
                previous = len(self.routes)
                super().run(*args[:3], 'add', *args[4:], input_data=input_data)
                row = self.routes.pop()
                assert len(self.routes) == previous
                self.routes = [old for old in self.routes if
                    (old['dst'], old.get('metric', 0)) != (row['dst'], row.get('metric', 0))]
                self.routes.append(row)
                return ''
            # Kernel route flags describe live carrier state, not delete keys.
            for row in self.routes:
                row['flags'] = []
        return super().run(*args, input_data=input_data)


class CarrierTests(unittest.TestCase):
    def setUp(self):
        self.kernel = Kernel()
        self.current = PHYSICAL
        self.network = SimpleNamespace(spec=SPEC, command=self.kernel, verify=Mock(), _empty=Mock())
        self.journal = Journal(base_record(SPEC))
        self.wrapped = JournalNetwork(self.journal, self.network)
        read = Path.read_text
        patches = [patch('karyo_profiles.forwarding.discover_underlay', side_effect=lambda command: self.current),
            patch('karyo_profiles.forwarding.Path.read_text', new=lambda path, *a, **k:
                '1\n' if str(path) == '/proc/sys/net/ipv4/ip_forward' else read(path, *a, **k))]
        for item in patches:
            item.start()
            self.addCleanup(item.stop)
        self.forwarding = JournalForwarding.start(self.journal, self.wrapped, PHYSICAL)
        self.network._empty.reset_mock()
        self.network._empty.side_effect = NetworkError('The desktop has live consumers')
        record = self.journal.record(SPEC.token)
        self.journal.change(record, dict(record, dns=DNSJournal().value['dns']))
        files = SimpleNamespace(_guard=Mock(),
            plan=ResolverPlan(SPEC, OLD, 1002, 990, '/run/karyo/fixture'))
        self.dns = JournalDNS(self.journal, self.wrapped, files, SimpleNamespace(verify=Mock()))
        self.link = Link()
        # The function's default factory was captured at definition time.
        import karyo_profiles.route_refresh as module
        from karyo_profiles.dns_refresh import refresh_dns
        wrapper = lambda dns, target=None, **options: refresh_dns(dns, target,
            link_factory=lambda unused: self.link, **options)
        item = patch.object(module, 'refresh_dns', side_effect=wrapper)
        item.start()
        self.addCleanup(item.stop)

    def apply(self, target=NEW):
        self.current = target.physical
        return refresh_routes(self.forwarding, self.dns, target, current=lambda command: self.current)

    def test_address_gateway_and_interface_change_preserves_consumers_and_selectors(self):
        original_rules = deepcopy(self.kernel.rules)
        self.assertTrue(self.apply())
        self.assertEqual(self.forwarding.gateway.physical, NEW.physical)
        self.assertEqual(self.dns.files.plan.seed, NEW)
        self.assertEqual(self.kernel.rules, original_rules)
        self.assertEqual(self.kernel.mode, 'eth1')
        self.assertEqual(set(self.kernel.route_phases), {'paused'})
        self.network._empty.assert_not_called()
        self.assertNotIn('refresh', self.journal.current['forwarding'])
        self.forwarding.verify()

    def test_gateway_only_change_keeps_connected_route_and_updates_dns(self):
        target = replace(OLD, physical=replace(PHYSICAL, gateway='192.0.2.254'))
        self.assertTrue(self.apply(target))
        self.assertEqual(self.dns.files.plan.seed, target)
        self.assertEqual(len(self.kernel.routes), 4)
        self.assertEqual(self.kernel.mode, 'eth0')
        self.forwarding.verify()

    def test_new_address_on_the_same_interface_keeps_the_existing_table(self):
        target = replace(NEW, physical=replace(NEW.physical, interface='eth0', index=2))
        handle = self.forwarding.gateway.firewall_handle
        self.assertTrue(self.apply(target))
        self.assertEqual(self.forwarding.gateway.firewall_handle, handle)
        self.assertEqual(self.kernel.mode, 'eth0')
        self.assertEqual(self.dns.files.plan.seed, target)
        self.forwarding.verify()

    def test_removed_old_address_routes_are_reconciled_from_the_remaining_base(self):
        self.kernel.routes = [row for row in self.kernel.routes
            if row.get('dev') != PHYSICAL.interface]
        self.assertTrue(self.apply())
        self.assertEqual(len(self.kernel.routes), 4)
        self.forwarding.verify()

    def test_reacquired_same_address_restores_only_missing_physical_routes(self):
        original_rules=deepcopy(self.kernel.rules)
        self.kernel.routes=[row for row in self.kernel.routes if row.get('dev')!=PHYSICAL.interface]
        self.assertTrue(lost_physical_routes(self.forwarding))
        self.assertTrue(self.apply(OLD))
        self.assertEqual(self.kernel.rules,original_rules)
        self.assertFalse(lost_physical_routes(self.forwarding))
        self.forwarding.verify()

    def test_transient_route_error_reattaches_without_recreating_the_graph(self):
        self.kernel.fail_default_once = True
        with self.assertRaises(TimeoutError):
            self.apply()
        self.assertEqual(self.journal.current['forwarding']['refresh']['step'], 2)
        self.assertEqual(self.kernel.mode, 'paused')
        self.forwarding = JournalForwarding.restore(self.journal, self.wrapped)
        self.assertTrue(self.apply())
        self.forwarding.verify()

    def test_another_carrier_can_replace_an_unfinished_transition(self):
        self.kernel.fail_default_once = True
        with self.assertRaises(TimeoutError):
            self.apply()
        self.assertTrue(self.apply(THIRD))
        self.assertEqual(self.forwarding.gateway.physical, THIRD.physical)
        self.assertEqual(self.dns.files.plan.seed, THIRD)
        self.assertEqual(self.kernel.mode, 'eth2')
        self.forwarding.verify()

    def test_carrier_changes_again_after_dns_was_applied_before_publication(self):
        self.current = THIRD.physical
        from karyo_profiles.journal import JournalRetained
        with self.assertRaisesRegex(JournalRetained, 'changed again'):
            refresh_routes(self.forwarding, self.dns, NEW, current=lambda command: self.current)
        self.assertEqual(self.journal.current['forwarding']['refresh']['step'], 5)
        self.assertEqual(self.dns.files.plan.seed, NEW)
        self.assertTrue(self.apply(THIRD))
        self.assertEqual(self.dns.files.plan.seed, THIRD)
        self.forwarding.verify()

    def test_offline_profile_close_removes_only_its_pending_graph(self):
        original_rules = deepcopy([row for row in self.kernel.rules
            if row['priority'] not in self.forwarding.gateway.priorities])
        self.kernel.fail_default_once = True
        with self.assertRaises(TimeoutError):
            self.apply()
        record = self.journal.record(SPEC.token)
        closed = dict(record)
        closed.pop('dns')
        self.journal.change(record, closed)
        self.network._empty.side_effect = None
        self.forwarding.close()
        self.assertFalse(self.kernel.routes)
        self.assertIsNone(self.kernel.firewall)
        self.assertNotIn('forwarding', self.journal.current)
        self.assertEqual(self.kernel.rules, original_rules)


if __name__ == '__main__':
    unittest.main()
