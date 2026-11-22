# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise mount identity, foreign ownership and filesystem metadata boundaries."""
import importlib.util
import json
from types import SimpleNamespace
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('shared_folders', Path(__file__).resolve().parents[1] / 'mount_shared_folders.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MountOwnership(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.target = root / 'mnt/hgfs'
        self.target.parent.mkdir()
        self.runtime = root / 'runtime'
        self.runtime.mkdir(mode=0o700)
        self.text, self.commands = '', []
        self.unique_id = 2147485000
        self.manager = module.SharedFolders(self.target, self.runtime, os.getuid(), self.native_command, lambda: self.text,
            lambda: {int(line.split()[0]): self.unique_id for line in self.text.splitlines()})

    def mount(self, identity=42, device='0:71', fstype='fuse.vmhgfs-fuse', source='vmhgfs-fuse'):
        return f'{identity} 1 {device} / {self.target} rw,nosuid,nodev - {fstype} {source} rw,user_id=0\n'

    def native_command(self, argv, **kwargs):
        self.commands.append(argv)
        if argv[0].endswith('/vmhgfs-fuse'):
            self.unique_id += 1
            self.text = self.mount()
        elif argv[0].endswith('/umount'):
            self.text = ''
        else:
            self.fail('Unexpected native program')

    def test_owned_start_repeat_stop_preserves_directory_and_files(self):
        self.manager.start()
        saved = self.manager.state.read_bytes()
        self.assertEqual(stat.S_IMODE(self.manager.state.stat().st_mode), 0o600)
        self.assertIn('allow_other,nosuid,nodev', self.commands[0][-1])
        keep = self.target / 'keep';keep.write_text('existing guest file')
        self.manager.start()
        self.assertEqual(self.manager.state.read_bytes(), saved)
        self.assertEqual(len(self.commands), 1)
        self.manager.stop()
        self.assertEqual(self.commands[-1], ['/usr/bin/umount', '--no-canonicalize', '--', str(self.target)])
        self.assertEqual(keep.read_text(), 'existing guest file')
        self.assertTrue(self.target.is_dir())
        self.assertFalse(self.manager.state.exists())
        self.manager.stop()
        self.assertEqual(len(self.commands), 2)

    def test_existing_admin_mount_is_never_adopted_or_unmounted(self):
        self.text = self.mount(fstype='ext4', source='/dev/admin')
        self.manager.start()
        self.manager.stop()
        self.assertEqual(self.commands, [])
        self.assertFalse(self.manager.state.exists())

    def test_inaccessible_foreign_fuse_is_detected_without_target_stat(self):
        self.text = self.mount()
        native_directory = module.directory
        def inaccessible_target(path, owner):
            if path == self.target:
                raise OSError(5, 'Input/output error', str(path))
            return native_directory(path, owner)
        with patch.object(module, 'directory', side_effect=inaccessible_target):
            self.manager.start()
            self.manager.stop()
        self.assertEqual(self.commands, [])
        self.assertFalse(self.manager.state.exists())

    def test_disappeared_or_replaced_mount_is_not_unmounted(self):
        for replacement in ('', self.mount(identity=99), self.mount(device='0:99'),
                            self.mount(fstype='ext4', source='/dev/admin')):
            self.text = ''
            self.manager.start();self.commands.clear()
            self.text = replacement
            self.manager.stop()
            self.assertEqual(self.commands, [])
            self.assertEqual(self.text, replacement)
            self.assertFalse(self.manager.state.exists())

    def test_reused_old_id_and_device_do_not_own_a_replacement_fuse(self):
        self.manager.start();self.commands.clear()
        original = self.manager.load()
        self.unique_id += 1  # New mount, same old ID/device/type/source.
        self.assertNotEqual(original, self.manager.current())
        self.manager.stop()
        self.assertEqual(self.commands, [])
        self.assertTrue(self.text)
        self.assertFalse(self.manager.state.exists())

    def test_missing_kernel_identity_fails_before_creating_a_mount(self):
        def unsupported():raise OSError(38, 'listmount unavailable')
        self.manager.identifiers = unsupported
        with self.assertRaises(OSError):self.manager.start()
        self.assertEqual(self.commands, [])
        self.assertFalse(self.target.exists())

    def test_kernel_identity_protocol_is_explicit_numeric_and_unambiguous(self):
        def reader(record):
            def command(argv, **kwargs):
                self.assertIn('--kernel=listmount', argv)
                self.assertEqual(argv[-1], 'ID,UNIQ-ID')
                self.assertNotIn('--target', argv)
                return SimpleNamespace(stdout=json.dumps(record))
            return command
        value={'filesystems':[{'id':42,'uniq-id':2147485000},{'id':'43','uniq-id':'2147485001'}]}
        self.assertEqual(module.kernel_mount_ids(reader(value)), {42:2147485000,43:2147485001})
        for bad in ({}, {'filesystems':[]}, {'filesystems':[{'id':42}]},
                    {'filesystems':[{'id':True,'uniq-id':2147485000}]},
                    {'filesystems':[{'id':42,'uniq-id':0}]},
                    {'filesystems':[{'id':42,'uniq-id':2147485000}]*2},
                    {'filesystems':[{'id':42,'uniq-id':2147485000},{'id':43,'uniq-id':2147485000}]}):
            with self.subTest(record=bad), self.assertRaises(ValueError):
                module.kernel_mount_ids(reader(bad))

    def test_stale_valid_metadata_allows_a_new_owned_mount(self):
        self.manager.start()
        self.text = ''
        self.manager.start()
        self.assertEqual(len(self.commands), 2)
        self.assertEqual(self.manager.load(), self.manager.current())

    def test_stacked_and_malformed_mountinfo_fail_before_native_writes(self):
        for text in ('not mountinfo', '4 1 invalid / '+str(self.target)+' rw - fuse.vmhgfs-fuse vmhgfs-fuse rw',
                     self.mount() + self.mount(identity=44)):
            self.text = text
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.manager.start()
            self.assertEqual(self.commands, [])

    def test_symlink_and_writable_mount_directories_fail(self):
        self.target.symlink_to(self.runtime, target_is_directory=True)
        with self.assertRaises(ValueError):self.manager.start()
        self.target.unlink();self.target.mkdir();self.target.chmod(0o777)
        with self.assertRaises(ValueError):self.manager.start()
        self.assertEqual(self.commands, [])

    def test_unsafe_state_file_types_modes_links_and_content_fail(self):
        self.manager.start();valid=self.manager.state.read_bytes();self.text='';self.commands.clear()
        for content in (b'not json', b'{}', b'x'*4097,
                        valid.replace(b'"mount_id": 42', b'"mount_id": true')):
            self.manager.state.write_bytes(content)
            with self.subTest(content=content[:30]), self.assertRaises((ValueError, json.JSONDecodeError)):
                self.manager.start()
            self.assertEqual(self.commands, [])
        self.manager.state.write_bytes(valid);self.manager.state.chmod(0o644)
        with self.assertRaises(ValueError):self.manager.start()
        self.manager.state.chmod(0o600)
        extra=self.runtime/'alias';os.link(self.manager.state, extra)
        with self.assertRaises(ValueError):self.manager.start()
        extra.unlink();self.manager.state.unlink();self.manager.state.symlink_to(extra)
        with self.assertRaises(OSError):self.manager.start()
        self.assertEqual(self.commands, [])

    def test_save_failure_rolls_back_only_the_new_owned_mount(self):
        def fail(state):raise OSError('metadata write failed')
        self.manager.save=fail
        with self.assertRaises(OSError):self.manager.start()
        self.assertEqual(self.commands[-1], ['/usr/bin/umount', '--no-canonicalize', '--', str(self.target)])
        self.assertEqual(self.text, '')

    def test_mountinfo_decodes_escaped_paths_and_ignores_other_targets(self):
        target=Path('/mnt/hgfs name')
        text='42 1 0:71 / /mnt/hgfs\\040name rw - fuse.vmhgfs-fuse vmhgfs-fuse rw\n'
        self.assertEqual(module.mount_record(text,target)['mount_id'],42)
        self.assertIsNone(module.mount_record(text,Path('/mnt/other')))
