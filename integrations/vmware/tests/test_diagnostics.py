# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
from types import SimpleNamespace
import os
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_vmware import agent
from karyo_vmware.diagnostics import ProcessWait, WaitMonitor, vmusr_wait


class WaitDiagnosticsTests(unittest.TestCase):
    def test_sustained_wait_is_logged_once_and_recovery_is_reported(self):
        reader = Mock(return_value=ProcessWait(123, 50, 'D', 'fuse_wait_answer'))
        log = Mock()
        monitor = WaitMonitor(reader, log)
        for now in [0, 5, 10, 15]:
            monitor.poll(42, now)
        log.assert_not_called()
        monitor.poll(42, 20)
        self.assertIn('fuse_wait_answer', log.call_args.args[0])
        monitor.poll(42, 25)
        log.assert_called_once()
        reader.return_value = ProcessWait(123, 50, 'S', 'poll_schedule_timeout')
        monitor.poll(42, 30)
        self.assertIn('left an uninterruptible', log.call_args.args[0])
        self.assertEqual(log.call_count, 2)

    def test_process_start_identity_and_group_changes_restart_observation(self):
        reader = Mock(return_value=ProcessWait(123, 50, 'D', 'fuse_wait_answer'))
        log = Mock()
        monitor = WaitMonitor(reader, log)
        monitor.poll(42, 0)
        reader.return_value = ProcessWait(123, 60, 'D', 'fuse_wait_answer')
        monitor.poll(42, 20)
        log.assert_not_called()
        monitor.poll(43, 40)
        log.assert_not_called()
        monitor.poll(43, 60)
        log.assert_called_once()
        monitor.poll(None, 61)
        self.assertIsNone(monitor.identity)

    def test_sampling_and_missing_process_do_not_interrupt_helpers(self):
        reader = Mock(return_value=None)
        monitor = WaitMonitor(reader, Mock())
        monitor.poll(42, 0)
        monitor.poll(42, 1)
        reader.assert_called_once_with(42)
        monitor.poll(None, 2)
        reader.assert_called_once()

    def test_proc_reader_selects_only_the_captured_vmusr_group(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for pid, group, command in [(10, 9, b'vmtoolsd\0-n\0vmusr\0'),
                                        (11, 42, b'python3\0'),
                                        (12, 42, b'/usr/bin/vmtoolsd\0--name=vmusr\0')]:
                path = root / str(pid)
                path.mkdir()
                fields = ['D', '1', str(group)] + ['0'] * 16 + ['99']
                (path / 'stat').write_text(f'{pid} (tool worker) ' + ' '.join(fields))
                (path / 'cmdline').write_bytes(command)
                (path / 'wchan').write_text('fuse_wait_answer\n')
            self.assertEqual(vmusr_wait(42, root), ProcessWait(12, 99, 'D', 'fuse_wait_answer'))

    def test_coordinator_resets_diagnostics_when_it_releases_foreground_ownership(self):
        monitor = Mock()
        children = []
        def factory(*unused):
            child = SimpleNamespace(process=SimpleNamespace(pid=123), alive=lambda: True, stop=Mock())
            children.append(child)
            return child
        value = agent.Agent([['vmusr']], clock=lambda: 10, binder=lambda: os.open('/dev/null', os.O_RDONLY),
                            factory=factory, external=lambda: False, monitor=monitor)
        try:
            value.tick(True)
            value.tick(True)
            monitor.poll.assert_called_with(123, 10)
            value.tick(False)
            monitor.poll.assert_called_with(None, 10)
            children[0].stop.assert_called_once()
            self.assertIsNone(value.lock)
        finally:
            value.stop()
