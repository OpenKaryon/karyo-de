# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import pwd
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_win32 import prefix_import as imports
from karyo_win32.policy import LaunchError


@unittest.skipUnless(os.name == 'posix', 'Prefix import uses native Linux descriptors/renameat2')
class PrefixImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.home = self.root / 'current-home'
        self.home.mkdir()
        self.source = self.root / 'legacy' / 'session-1'
        self.source.mkdir(parents=True)
        self.registry = self.source / 'system.reg'
        self.registry.write_bytes(b'WINE REGISTRY Version 2\n#arch=win64\n')
        (self.source / 'user.reg').write_bytes(b'WINE REGISTRY Version 2\n')
        self.profile = self.source / 'drive_c/users/former-user'
        self.profile.mkdir(parents=True)
        (self.profile / 'preferences.ini').write_text('Preserved: Пример\n')
        self.outside = self.root / 'external-data'
        self.outside.write_text('External data is not copied')
        devices = self.source / 'dosdevices'
        devices.mkdir()
        (devices / 'c:').symlink_to(self.source / 'drive_c')
        (devices / 'z:').symlink_to('/')
        (self.profile / 'external').symlink_to(os.path.relpath(self.outside, self.profile))
        self.identifier = 'imported-app'
        self.destination = self.home / '.local/share/karyo/windows/prefixes' / self.identifier
        self.environment = {'HOME': str(self.home), 'XDG_DATA_HOME': str(self.home / '.local/share')}
        self.native_idle = imports.wait_idle
        self.idle = self.enterContext(patch.object(imports, 'wait_idle'))

    def snapshot(self):
        fd = imports.open_directory(self.source)
        try:
            return imports.scan(fd)
        finally:
            os.close(fd)

    def run_import(self, **kwargs):
        return imports.import_prefix(self.source, self.home, self.identifier, self.environment, **kwargs)

    def test_preview_neither_creates_storage_nor_starts_wine(self):
        before = self.snapshot()
        plan, _ = imports.analyze(self.source, self.home, self.identifier)
        self.assertEqual(plan['architecture'], 'win64')
        self.assertEqual(plan['files'], 3)
        self.assertEqual(plan['symlinks'], 3)
        self.assertFalse((self.home / '.local').exists())
        self.assertEqual(self.snapshot(), before)
        self.idle.assert_not_called()

    def test_copy_preserves_registry_data_timestamps_and_private_owner(self):
        before = self.snapshot()
        source_bytes = self.registry.read_bytes()
        os.utime(self.profile / 'preferences.ini', ns=(123456789000, 123456789000))
        before = self.snapshot()
        result = self.run_import()
        self.assertEqual(result['status'], 'imported')
        self.assertFalse(result['architectureConverted'])
        self.assertEqual((self.destination / 'system.reg').read_bytes(), source_bytes)
        copied = self.destination / 'drive_c/users/former-user/preferences.ini'
        self.assertEqual(copied.read_bytes(), (self.profile / 'preferences.ini').read_bytes())
        self.assertEqual(copied.stat().st_mtime_ns, 123456789000)
        self.assertEqual(stat.S_IMODE(self.destination.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE(copied.stat().st_mode) & 0o077, 0)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(self.idle.call_count, 2)

    def test_internal_and_external_links_keep_meaning_without_copying_targets(self):
        self.run_import()
        drive = self.destination / 'dosdevices/c:'
        self.assertEqual(drive.resolve(), self.destination / 'drive_c')
        self.assertFalse(os.path.isabs(os.readlink(drive)))
        self.assertEqual(os.readlink(self.destination / 'dosdevices/z:'), '/')
        linked = self.destination / 'drive_c/users/former-user/external'
        self.assertEqual(os.readlink(linked), str(self.outside))
        self.assertTrue(linked.is_symlink())
        self.assertFalse((self.destination / self.outside.name).exists())
        self.assertEqual(self.outside.read_text(), 'External data is not copied')

    def test_changed_unix_username_aliases_existing_windows_data_only_in_copy(self):
        current = pwd.getpwuid(os.getuid()).pw_name
        self.run_import()
        alias = self.destination / 'drive_c/users' / current
        self.assertTrue(alias.is_symlink())
        self.assertEqual(alias.resolve(), self.destination / 'drive_c/users/former-user')
        self.assertFalse((self.source / 'drive_c/users' / current).exists())

    def test_multiple_windows_profiles_require_explicit_selection(self):
        (self.source / 'drive_c/users/another-user').mkdir()
        plan, _ = imports.analyze(self.source, self.home, self.identifier)
        self.assertEqual(plan['userAlias'], 'choice-required')
        with self.assertRaisesRegex(LaunchError, 'windows-user'):
            self.run_import()
        self.assertFalse((self.home / '.local').exists())
        self.run_import(selected='former-user')
        self.assertEqual(os.readlink(self.destination / 'drive_c/users' / pwd.getpwuid(os.getuid()).pw_name), 'former-user')

    def test_existing_profile_and_registered_id_are_never_replaced(self):
        self.destination.mkdir(parents=True)
        sentinel = self.destination / 'keep.txt'
        sentinel.write_text('user edits')
        with self.assertRaisesRegex(LaunchError, 'существует'):
            self.run_import()
        self.assertEqual(sentinel.read_text(), 'user edits')
        self.assertFalse(list(self.destination.parent.glob('.import-*')))
        self.destination.rename(self.destination.with_name('kept-profile'))
        record = self.destination.parent.parent / 'applications/imported-app.json'
        record.parent.mkdir()
        record.write_text('Existing launcher data')
        with self.assertRaisesRegex(LaunchError, 'ярлыком'):
            self.run_import()
        self.assertEqual(record.read_text(), 'Existing launcher data')
        self.assertFalse(self.destination.exists())

    def test_atomic_publication_refuses_a_concurrent_empty_or_nonempty_destination(self):
        publish = imports.publish
        for with_file in (False, True):
            with self.subTest(with_file=with_file):
                def winner(parent, temporary, identifier):
                    self.destination.mkdir()
                    if with_file:
                        (self.destination / 'winner.txt').write_text('concurrent launch')
                    return publish(parent, temporary, identifier)
                with patch.object(imports, 'publish', side_effect=winner):
                    with self.assertRaisesRegex(LaunchError, 'существует'):
                        self.run_import()
                self.assertFalse(list(self.destination.parent.glob('.import-*')))
                if with_file:
                    self.assertEqual((self.destination / 'winner.txt').read_text(), 'concurrent launch')
                    (self.destination / 'winner.txt').unlink()
                self.destination.rmdir()

    def test_source_mutation_aborts_copy_and_keeps_original_changes(self):
        copy = imports.copy_prefix
        def mutation(*args):
            result = copy(*args)
            self.registry.write_bytes(b'WINE REGISTRY Version 2\n#arch=win64\nchanged\n')
            return result
        with patch.object(imports, 'copy_prefix', side_effect=mutation):
            with self.assertRaisesRegex(LaunchError, 'изменился'):
                self.run_import()
        self.assertFalse(self.destination.exists())
        self.assertFalse(list(self.destination.parent.glob('.import-*')))
        self.assertIn(b'changed', self.registry.read_bytes())

    def test_busy_prefix_is_not_copied(self):
        self.idle.side_effect = LaunchError('Закройте Wine-приложения')
        with self.assertRaisesRegex(LaunchError, 'Закройте'):
            self.run_import()
        self.assertFalse((self.home / '.local').exists())

    def test_special_files_bad_registry_and_foreign_source_owner_are_refused(self):
        fifo = self.source / 'busy-pipe'
        os.mkfifo(fifo)
        with self.assertRaisesRegex(LaunchError, 'Специальные'):
            self.run_import()
        fifo.unlink()
        self.registry.write_bytes(b'not a Wine registry')
        with self.assertRaisesRegex(LaunchError, 'system.reg'):
            self.run_import()
        self.registry.write_bytes(b'WINE REGISTRY Version 2\n#arch=\xff\n')
        with self.assertRaisesRegex(LaunchError, 'архитектура'):
            self.run_import()
        self.registry.write_bytes(b'WINE REGISTRY Version 2\n#arch=win64\n')
        with patch.object(imports.os, 'getuid', return_value=os.getuid() + 1):
            with self.assertRaises(LaunchError):
                self.run_import()
        self.assertFalse((self.home / '.local').exists())

    def test_unknown_architecture_is_preserved_without_registry_rewrite(self):
        self.registry.write_bytes(b'WINE REGISTRY Version 2\n')
        result = self.run_import()
        self.assertEqual(result['architecture'], 'unknown')
        self.assertEqual((self.destination / 'system.reg').read_bytes(), self.registry.read_bytes())

    def test_overlapping_sources_and_invalid_identifiers_are_refused(self):
        for identifier in ('../escape', '', '.hidden'):
            with self.subTest(identifier=identifier), self.assertRaises(LaunchError):
                imports.analyze(self.source, self.home, identifier)
        with self.assertRaises(LaunchError):
            imports.analyze(self.home, self.home, self.identifier)
        nested = self.destination / 'nested'
        nested.mkdir(parents=True)
        with self.assertRaises(LaunchError):
            imports.analyze(nested, self.home, self.identifier)

    def test_idle_check_uses_only_native_server_wait_with_bounded_timeout(self):
        # Exercise the original callable without ever starting Windows code.

        with patch.object(imports.shutil, 'which', return_value='/runtime/wineserver'), \
                patch.object(imports.subprocess, 'run', side_effect=subprocess.TimeoutExpired('server', 5)) as call:
            with self.assertRaisesRegex(LaunchError, 'Закройте'):
                self.native_idle(self.source, {'WINEARCH': 'win32'})
            self.assertEqual(call.call_args.args[0], ['/runtime/wineserver', '-w'])
            self.assertEqual(call.call_args.kwargs['timeout'], 5)
            self.assertNotIn('WINEARCH', call.call_args.kwargs['env'])
