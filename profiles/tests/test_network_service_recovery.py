# SPDX-License-Identifier: GPL-3.0-or-later
"""Durable worker replacement and bounded ordinary lifecycle retries."""
from copy import deepcopy
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.journal_network_service import JournalNetworkService
from karyo_profiles.network_service_watch import NetworkServiceMonitor
from test_journal import WORK, JournalFixture
from test_journal_dns import configuration, proof
from test_main_entry import receipt


@unittest.skipUnless(os.geteuid() == 0, 'root-owned recovery journal')
class WorkerRecoveryTest(JournalFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.token = self.journal.begin_main(WORK)
        before = self.journal.record(self.token)
        network = {'namespace': [1, 2], 'indices': [9, 2],
                   'macs': ['02:00:00:00:00:01', '02:00:00:00:00:02'], 'contents': 'tunnel'}
        dns = configuration()
        dns.update(view='network-manager', pending=None, files={'directory': [2, 3],
                   'files': {'bus.conf': [2, 4], 'resolved.conf': [2, 5]}}, bus=proof(),
                   resolver=dict(proof(), runtime=[[1, 5], [1, 6]]))
        self.service = {'kind': 'network-manager', 'pending': None, 'clients': True,
                        'state': {'directory': [2, 10], 'stamp': [2, 11], 'home': [2, 12]},
                        'config': {'directory': [2, 13], 'file': [2, 14], 'backend': 'keyfile'},
                        'unit': proof()}
        self.journal.change(before, dict(before, pending=None, network=network, dns=dns,
                                        network_service=self.service, main_login=dict(receipt(), network_client=True)))
        self.dns = SimpleNamespace(token=self.token, services=Mock())
        self.scope = SimpleNamespace(network=Mock(), state=Mock(), unit=Mock(closed=True),
                                     directory=self.root/'missing-worker', release_handles=Mock(),
                                     start_prepared=Mock(), verify=Mock(), remove_config=Mock())
        self.scope.unit.info.return_value = {'LoadState': 'not-found'}
        self.new_unit = Mock(closed=False)
        self.new_proof = dict(proof(), invocation='de'*16, pid=456, start=789)
        for item in (patch('karyo_profiles.journal_network_service.state_receipt', return_value=self.service['state']),
                     patch('karyo_profiles.journal_network_service.config_receipt', return_value=self.service['config']),
                     patch('karyo_profiles.journal_network_service.unit_receipt', return_value=self.new_proof),
                     patch('karyo_profiles.journal_network_service.OwnedUnit', return_value=self.new_unit)):
            item.start();self.addCleanup(item.stop)

    def owner(self):return JournalNetworkService(self.journal, self.dns, self.scope)

    def test_replacement_intent_precedes_launch_and_retains_context(self):
        before = self.journal.record(self.token)
        def launch():
            value = self.journal.record(self.token)['network_service']
            self.assertEqual(value['pending'], 'restart-start')
            self.assertIsNone(value['unit'])
            self.assertEqual(self.journal.snapshot()['schema'], 19)
        self.scope.start_prepared.side_effect = launch
        with patch('karyo_profiles.network_uplink.prepare_uplink') as uplink:
            self.owner().restart()
            uplink.assert_called_once_with(self.scope)
        current = self.journal.record(self.token)
        self.assertEqual(current['network_service']['unit'], self.new_proof)
        self.assertIsNone(current['network_service']['pending'])
        for key in ('network', 'dns', 'main_login'):
            self.assertEqual(current[key], before[key])

    def test_acknowledged_uplink_failure_resumes_after_journal_reopen(self):
        with patch('karyo_profiles.network_uplink.prepare_uplink', side_effect=RuntimeError('link settling')):
            with self.assertRaisesRegex(RuntimeError, 'link settling'):self.owner().restart()
        self.assertEqual(self.journal.record(self.token)['network_service']['pending'], 'restart-uplink')
        self.reopen()
        self.scope.start_prepared.reset_mock()
        with patch('karyo_profiles.network_uplink.prepare_uplink'):
            self.owner().restart()
        self.scope.start_prepared.assert_not_called()
        self.assertEqual(self.journal.record(self.token)['network_service']['unit'], self.new_proof)
        self.assertIsNone(self.journal.record(self.token)['network_service']['pending'])

    def test_interrupted_prelaunch_receipt_can_retire_after_natural_login_exit(self):
        with patch.object(self.scope, 'start_prepared', side_effect=RuntimeError('launch interrupted')):
            with self.assertRaisesRegex(RuntimeError, 'launch interrupted'):self.owner().restart()
        self.reopen()
        before = self.journal.record(self.token)
        after = deepcopy(before);after.pop('main_login');self.journal.change(before, after)
        self.scope.unit.closed = True
        self.scope.unit.info.return_value = {'LoadState': 'not-found'}
        self.owner().close()
        self.assertNotIn('network_service', self.journal.record(self.token))
        self.scope.remove_config.assert_called_once_with(partial=True)


class WorkerMonitorTest(unittest.TestCase):
    def test_failed_restore_is_throttled_and_another_desktop_can_progress(self):
        now = [0]
        messages = []
        monitor = NetworkServiceMonitor(clock=lambda: now[0], report=messages.append)
        store = Mock();store.locked.return_value.__enter__ = Mock();store.locked.return_value.__exit__ = Mock()
        def scope():
            value = SimpleNamespace(network_service=SimpleNamespace(scope=SimpleNamespace(unit=Mock(closed=True))),
                                    journal=Mock(), verify_consumers=Mock())
            value.journal.record.return_value = {'session': {'pending': None}, 'network_service': {'pending': None}}
            return value
        first, second = scope(), scope()
        first.verify_consumers.side_effect = RuntimeError('worker still retiring')
        monitor.poll({'a': first, 'b': second}, store)
        monitor.poll({'a': first, 'b': second}, store)
        first.verify_consumers.assert_called_once();second.verify_consumers.assert_called_once()
        now[0] = 15
        monitor.poll({'a': first, 'b': second}, store)
        self.assertEqual(first.verify_consumers.call_count, 2)
        self.assertEqual(len(messages), 2)  # Repeated identical failure is not logged again.
