# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary provider replacement, interrupted acknowledgement and retirement."""
from copy import deepcopy
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.journal_netbird import JournalNetBird, refresh_optional
from karyo_profiles.network_service_watch import NetworkServiceMonitor
from karyo_profiles.provider_worker import restore_unit
from test_journal import WORK, JournalFixture
from test_journal_dns import configuration, proof
from test_journal_netbird import provider
from test_main_entry import receipt


@unittest.skipUnless(os.geteuid() == 0, 'root-owned recovery journal')
class ProviderRecoveryTest(JournalFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.token = self.journal.begin_main(WORK)
        before = self.journal.record(self.token)
        network = {'namespace': [4, 100], 'indices': [9, 2], 'contents': 'tunnel',
                   'macs': ['02:00:00:00:00:01', '02:00:00:00:00:02']}
        dns = configuration()
        dns.update(pending=None, files={'directory': [2, 3],
                   'files': {'bus.conf': [2, 4], 'resolved.conf': [2, 5]}}, bus=proof(),
                   resolver=dict(proof(), runtime=[[1, 5], [1, 6]]))
        self.provider = provider()
        self.journal.change(before, dict(before, pending=None, network=network, dns=dns,
                                        provider=self.provider, main_login=receipt()))
        self.dns = SimpleNamespace(token=self.token, services=Mock())
        self.scope = SimpleNamespace(network=Mock(), state=Mock(), unit=Mock(closed=True),
                                     directory=self.root/'missing-provider', release_handles=Mock(),
                                     start_prepared=Mock(), close=Mock())
        self.scope.unit.info.return_value = {'LoadState': 'not-found'}
        self.new_unit = Mock(closed=False)
        self.new_proof = deepcopy(self.provider['service'])
        self.new_proof['unit'].update(invocation='de'*16, pid=456, start=789)
        for item in (patch('karyo_profiles.journal_netbird.state_receipt', return_value=self.provider['state']),
                     patch('karyo_profiles.journal_netbird.service_receipt', return_value=self.new_proof),
                     patch('karyo_profiles.journal_netbird.OwnedUnit', return_value=self.new_unit)):
            item.start();self.addCleanup(item.stop)

    def owner(self):return JournalNetBird(self.journal, self.dns, self.scope)

    def test_intent_precedes_launch_and_preserves_network_dns_login_and_account_state(self):
        before = self.journal.record(self.token)
        def launch():
            value = self.journal.record(self.token)['provider']
            self.assertEqual(value['pending'], 'restart-start')
            self.assertIsNone(value['service'])
            self.assertEqual(self.journal.snapshot()['schema'], 20)
            self.assertIsNone(self.scope.endpoint_identity)
        self.scope.start_prepared.side_effect = launch
        self.owner().restart()
        current = self.journal.record(self.token)
        self.assertEqual(current['provider']['service'], self.new_proof)
        self.assertIsNone(current['provider']['pending'])
        for key in ('network', 'dns', 'main_login', 'account'):
            self.assertEqual(current[key], before[key])
        self.assertEqual(current['provider']['state'], before['provider']['state'])
        self.reopen()
        self.assertEqual(self.journal.record(self.token), current)

    def test_interrupted_prelaunch_can_resume_after_reopen_or_retire_after_login_exit(self):
        self.scope.start_prepared.side_effect = TimeoutError('launch interrupted')
        with self.assertRaises(TimeoutError):self.owner().restart()
        self.assertIsNone(self.journal.record(self.token)['provider']['service'])
        self.reopen()
        self.scope.unit = Mock(closed=True)
        self.scope.unit.info.return_value = {'LoadState': 'not-found'}
        self.scope.start_prepared.side_effect = None
        self.owner().restart()
        self.assertIsNone(self.journal.record(self.token)['provider']['pending'])
        before = self.journal.record(self.token)
        after = deepcopy(before);after.pop('main_login')
        after['provider'].update(pending='restart-start', service=None)
        self.journal.change(before, after)
        self.scope.unit.closed = True
        self.owner().close()
        self.assertNotIn('provider', self.journal.record(self.token))

    def test_dead_worker_restore_needs_complete_unit_and_runtime_retirement(self):
        self.scope.unit.name = 'karyo-netbird-' + self.token + '.service'
        self.scope.unit.pidfd = None
        self.scope.unit.cgroup_pids.return_value = set()
        with patch('karyo_profiles.provider_worker.process_gone', return_value=True):
            restore_unit(self.scope, self.provider['service'])
        self.assertTrue(self.scope.unit.closed)
        self.scope.unit.process.open.assert_not_called()

    def test_ordinary_refresh_replaces_only_after_the_old_observer_is_released(self):
        old = SimpleNamespace(scope=self.scope, release_handles=Mock())
        current = Mock()
        with patch('karyo_profiles.journal_netbird.restore_optional', return_value=current):
            self.assertIs(refresh_optional(self.journal, self.dns, old), current)
        old.release_handles.assert_called_once_with()
        current.restart.assert_called_once_with()


class ProviderMonitorTest(unittest.TestCase):
    def test_live_network_worker_does_not_hide_failed_provider_and_retry_is_bounded(self):
        now = [0]
        monitor = NetworkServiceMonitor(clock=lambda: now[0], report=Mock())
        network_unit = Mock(closed=False, pidfd=12)
        network_unit.process.exited.return_value = False
        scope = SimpleNamespace(network_service=SimpleNamespace(scope=SimpleNamespace(unit=network_unit)),
            provider=SimpleNamespace(scope=SimpleNamespace(unit=Mock(closed=True))),
            journal=Mock(), verify_consumers=Mock())
        scope.journal.record.return_value = {'session': {'pending': None},
            'network_service': {'pending': None}, 'provider': {'pending': None}}
        store = Mock();store.locked.return_value.__enter__ = Mock();store.locked.return_value.__exit__ = Mock()
        scope.verify_consumers.side_effect = RuntimeError('cgroup still retiring')
        monitor.poll({'a': scope}, store);monitor.poll({'a': scope}, store)
        scope.verify_consumers.assert_called_once()
        now[0] = 15
        scope.verify_consumers.side_effect = None
        monitor.poll({'a': scope}, store)
        self.assertEqual(scope.verify_consumers.call_count, 2)
