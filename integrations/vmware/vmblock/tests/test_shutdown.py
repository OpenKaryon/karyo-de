# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import shutdown_guard as guard

MOUNTINFO = '42 31 0:71 / /run/vmblock-fuse rw - fuse.vmware-vmblock vmware-vmblock rw\n'


class ShutdownReleaseTests(unittest.TestCase):
    def test_shutdown_jobs_are_distinct_from_restart_or_logout(self):
        self.assertTrue(guard.shutdown_pending('123 shutdown.target start waiting\n'))
        self.assertFalse(guard.shutdown_pending('123 karyo-vmblock-shutdown.service stop running\n'))
        self.assertFalse(guard.shutdown_pending('123 session-3.scope stop running\n'))

    def test_native_mount_metadata_selects_the_single_vmblock_connection(self):
        self.assertEqual(guard.connection(MOUNTINFO), guard.Connection('42', '71'))
        self.assertIsNone(guard.connection(MOUNTINFO.replace('fuse.vmware-vmblock', 'fuse.vmhgfs-fuse')))
        self.assertIsNone(guard.connection(MOUNTINFO + MOUNTINFO))
        self.assertIsNone(guard.connection(''))

    def test_existing_control_receives_abort_without_accessing_file_transfer_mount(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / '71').mkdir()
            abort = root / '71/abort'
            abort.write_text('')
            self.assertEqual(guard.abort_vmblock(lambda: MOUNTINFO, root), '71')
            self.assertEqual(abort.read_bytes(), b'1\n')

    def test_mount_replacement_and_already_unmounted_connection_are_ordinary_noops(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertIsNone(guard.abort_vmblock(lambda: MOUNTINFO, root))
            (root / '71').mkdir()
            abort = root / '71/abort'
            abort.write_text('')
            reads = iter([MOUNTINFO, MOUNTINFO.replace('42 31', '43 31')])
            self.assertIsNone(guard.abort_vmblock(lambda: next(reads), root))
            self.assertEqual(abort.read_bytes(), b'')

    def test_normal_stop_does_not_attempt_to_release_a_connection(self):
        with patch.object(guard.subprocess, 'check_output', return_value=''), patch.object(guard, 'abort_vmblock') as abort:
            self.assertEqual(guard.main(), 0)
            abort.assert_not_called()
