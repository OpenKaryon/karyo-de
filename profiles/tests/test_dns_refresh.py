# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary live-DNS updates and retry without restarting namespace clients."""
from contextlib import nullcontext
from copy import deepcopy
from dataclasses import replace
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.dns import DNSSeed, ResolverPlan
from karyo_profiles.dns_refresh import cancel_refresh, encoded, refresh_dns, wire
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_dns_record import validate_dns
from karyo_profiles.networks import NetworkCommand, NetworkError, NetworkSpec
from karyo_profiles.network_watch import DNSRefreshMonitor
from karyo_profiles.underlay import PhysicalUnderlay

PHYSICAL = PhysicalUnderlay('eth0', 2, '192.0.2.2', 24, '192.0.2.1', 100)
OLD = DNSSeed(PHYSICAL, ('192.0.2.1',), ('old.example',))
NEW = DNSSeed(PHYSICAL, ('192.0.2.53',), ('new.example',))
SPEC = NetworkSpec(1002, 'ab' * 16)


class Journal:
    def __init__(self):
        proof = {'invocation': 'cd' * 16, 'pid': 345, 'start': 67, 'exe': [1, 2],
                 'namespace': [1, 3], 'runtime': [[1, 4]]}
        self.value = {'token': SPEC.token, 'dns': {
            'seed': encoded(OLD), 'gid': 1002, 'service_uid': 990, 'pending': None,
            'files': {'directory': [1, 5], 'files': {'bus.conf': [1, 6], 'resolved.conf': [1, 7]}},
            'bus': proof, 'resolver': dict(proof, runtime=[[1, 8], [1, 9]])}}

    def record(self, token):
        assert token == SPEC.token
        return deepcopy(self.value)

    def change(self, before, after):
        assert before == self.value
        validate_dns(after['dns'], SPEC, 1002)
        self.value = deepcopy(after)


class Link:
    def __init__(self):
        self.values = dict(wire(OLD), DefaultRoute=True)
        self.calls = []
        self.fail_domains_once = False

    def read(self, name):
        return deepcopy(self.values[name])

    def write(self, name, target):
        self.calls.append(name)
        if name == 'Domains' and self.fail_domains_once:
            self.fail_domains_once = False
            raise NetworkError('Private bus temporarily unavailable')
        self.values[name] = wire(target)[name]


class RefreshTests(unittest.TestCase):
    def setUp(self):
        self.journal = Journal()
        self.files = SimpleNamespace(_guard=Mock(),
            plan=ResolverPlan(SPEC, OLD, 1002, 990, '/run/karyo/fixture'))
        self.services = SimpleNamespace(verify=Mock())
        self.dns = JournalDNS(self.journal, SimpleNamespace(token=SPEC.token), self.files, self.services)
        self.link = Link()

    def apply(self, target=None):
        return refresh_dns(self.dns, target, link_factory=lambda dns: self.link)

    def test_live_update_preserves_resolver_object(self):
        old_services = self.dns.services
        self.assertTrue(self.apply(NEW))
        self.assertEqual(self.link.values, dict(wire(NEW), DefaultRoute=True))
        self.assertEqual(self.files.plan.seed, NEW)
        self.assertIs(self.dns.services, old_services)
        self.assertNotIn('refresh', self.journal.value['dns'])

    def test_unchanged_seed_performs_no_private_link_calls(self):
        factory = Mock()
        self.assertFalse(refresh_dns(self.dns, OLD, link_factory=factory))
        factory.assert_not_called()

    def test_transient_bus_failure_resumes_the_acknowledged_property(self):
        self.link.fail_domains_once = True
        with self.assertRaises(NetworkError):
            self.apply(NEW)
        self.assertEqual(self.journal.value['dns']['refresh']['step'], 1)
        self.assertEqual(self.files.plan.seed, OLD)
        self.assertTrue(self.apply())
        self.assertEqual(self.link.calls.count('DNS'), 1)
        self.assertEqual(self.files.plan.seed, NEW)

    def test_already_applied_next_property_is_acknowledged_without_repeating_it(self):
        self.journal.value['dns']['refresh'] = {'target': encoded(NEW), 'step': 0}
        self.link.values['DNS'] = wire(NEW)['DNS']
        self.assertTrue(self.apply())
        self.assertEqual(self.link.calls, ['Domains'])

    def test_metric_only_update_keeps_the_same_dns_values(self):
        target = replace(OLD, physical=replace(PHYSICAL, metric=200))
        self.assertTrue(self.apply(target))
        self.assertEqual(self.link.calls, [])
        self.assertEqual(self.files.plan.seed.physical.metric, 200)

    def test_closed_consumers_can_retire_a_pending_dns_only_update(self):
        self.journal.value['dns']['refresh'] = {'target': encoded(NEW), 'step': 1}
        cancel_refresh(self.dns)
        self.assertNotIn('refresh', self.journal.value['dns'])
        self.assertEqual(self.link.calls, [])


class MonitorTests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.scope = SimpleNamespace(graph=SimpleNamespace(closed=False), forwarding=None,
            journal=Journal(), dns=SimpleNamespace(files=SimpleNamespace(plan=SimpleNamespace(seed=OLD),
                network=SimpleNamespace(command=NetworkCommand()))))
        self.profiles = {SPEC.token: self.scope}
        self.store = SimpleNamespace(locked=nullcontext)
        self.read = Mock(return_value=NEW)
        self.apply = Mock(return_value=True)
        self.messages = []
        self.monitor = DNSRefreshMonitor(clock=lambda: self.now, read=self.read,
            apply=self.apply, report=self.messages.append)

    def test_regular_poll_applies_once_then_waits_for_the_next_inventory(self):
        self.monitor.poll(self.profiles, self.store)
        self.monitor.poll(self.profiles, self.store)
        self.assertEqual(self.read.call_count, 1)
        self.assertEqual(self.apply.call_count, 1)
        self.now = 15
        self.monitor.poll(self.profiles, self.store)
        self.assertEqual(self.read.call_count, 2)

    def test_temporary_discovery_outage_does_not_busy_loop_or_log_repeatedly(self):
        self.read.side_effect = NetworkError('No carrier')
        for self.now in (0, .5, 15, 16):
            self.monitor.poll(self.profiles, self.store)
        self.assertEqual(self.read.call_count, 2)
        self.assertEqual(len(self.messages), 1)
        self.apply.assert_not_called()

    def test_closed_desktops_do_not_trigger_discovery(self):
        self.scope.graph.closed = True
        self.monitor.poll(self.profiles, self.store)
        self.read.assert_not_called()

    def test_main_pam_entry_participates_without_a_child_graphical_launcher(self):
        del self.scope.graph
        self.scope.is_main = True
        self.scope.closed = False
        self.scope.peer = object()
        self.scope.token = SPEC.token
        self.scope.verify = Mock()
        self.scope.journal.value['main_login'] = {'pending': None}
        self.monitor.poll(self.profiles, self.store)
        self.scope.verify.assert_called_once()
        self.apply.assert_called_once()
        self.scope.journal.value['main_login']['pending'] = 'login-close'
        self.now = 15
        self.monitor.poll(self.profiles, self.store)
        self.assertEqual(self.read.call_count, 1)

    def test_routing_change_is_reported_for_the_separate_route_reconciler(self):
        self.read.return_value = replace(NEW, physical=replace(PHYSICAL, gateway='192.0.2.254'))
        self.monitor.poll(self.profiles, self.store)
        self.apply.assert_not_called()
        self.assertIn('route reconciliation required', self.messages[0])


if __name__ == '__main__':
    unittest.main()
