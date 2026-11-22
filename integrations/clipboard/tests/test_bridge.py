# SPDX-License-Identifier: GPL-3.0-or-later
"""File, text and PNG bridge format and transfer regression tests."""
import os
import importlib.machinery
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch, Mock
sys.dont_write_bytecode = True
path = Path(os.environ.get('KARYO_XCLIPBOARD_TEST_MODULES', str(Path(__file__).resolve().parents[1]))) / 'karyo_xclipboard/bridge.py'
loader = importlib.machinery.SourceFileLoader('clipboard_bridge', str(path))
bridge = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
loader.exec_module(bridge)

class BridgeTests(unittest.TestCase):

    def test_files_take_precedence_and_keep_encoded_names(self):
        uris = b'file:///home/user/Archive%20%231.zip\r\nfile:///home/user/%D0%A2%D0%B5%D1%81%D1%82.zip\r\n'
        read = Mock(return_value=uris)
        self.assertEqual(bridge.read_selection(b'text/plain\ntext/uri-list\n', read), (bridge.URI_TYPE, uris))
        read.assert_called_once_with(bridge.URI_TYPE, bridge.LIMIT)

    def test_gnome_file_copy_and_cut_become_copyable_uri_lists(self):
        for action in (b'copy', b'cut'):
            data = action + b'\nfile://localhost/home/user/a%20b.zip'
            self.assertEqual(bridge.read_selection(b'x-special/gnome-copied-files\n', lambda *_: data),
                             (bridge.URI_TYPE, b'file:///home/user/a%20b.zip\r\n'))

    def test_invalid_file_lists_never_turn_into_path_text(self):
        for value in (b'', b'file:relative.zip', b'file://remote/tmp/a.zip', b'https://example.com/a.zip',
                      b'file:///tmp/a%00.zip', b'file:///tmp/a.zip?query', b'\xff'):
            with self.subTest(value=value):
                self.assertIsNone(bridge.read_selection(b'text/uri-list\ntext/plain\n', lambda *_: value))
        self.assertIsNone(bridge.read_selection(b'text/uri-list\ntext/plain\n', lambda *_: None))

    def test_sensitive_hint_never_exports_a_supported_payload(self):
        self.assertIsNone(bridge.read_selection(b'x-kde-passwordManagerHint\ntext/plain\nimage/png\n',
            lambda *_: self.fail('Sensitive payload must not be requested')))

    def test_vmblock_paths_are_bridged_without_opening_files(self):
        with patch.object(Path, 'open', side_effect=AssertionError('File must stay lazy')):
            self.assertEqual(bridge.file_uris(b'# comment\r\nfile:///run/vmblock-fuse/blockdir/123/file.zip\r\n'),
                             b'file:///run/vmblock-fuse/blockdir/123/file.zip\r\n')

    def test_binary_png_preferred_over_text_and_preserved(self):
        png = bridge.PNG_SIGNATURE + b'\x00binary\xff\x00'
        reads = []

        def read(target, limit):
            reads.append((target, limit))
            return png
        self.assertEqual(bridge.read_selection(b'UTF8_STRING\nimage/png\n', read), ('image/png', png))
        self.assertEqual(reads, [('image/png', bridge.PNG_LIMIT)])

    def test_text_empty_text_and_unsupported(self):
        self.assertEqual(bridge.read_selection(b'UTF8_STRING\n', lambda *_: 'Привет'.encode()), (bridge.TEXT_TYPE, 'Привет'.encode()))
        self.assertEqual(bridge.read_selection(b'text/plain\n', lambda *_: b''), (bridge.TEXT_TYPE, b''))
        self.assertIsNone(bridge.read_selection(b'application/pdf\n', lambda *_: self.fail('unsupported transfer')))
        self.assertIsNone(bridge.read_selection(None, lambda *_: self.fail('no selection')))

    def test_bad_or_failed_png_does_not_replace_image_with_text(self):
        self.assertIsNone(bridge.read_selection(b'image/png\nUTF8_STRING\n', lambda *_: b'bad'))
        self.assertIsNone(bridge.read_selection(b'image/png\n', lambda *_: None))

    def test_type_is_part_of_deduplication(self):
        self.assertNotEqual(bridge.fingerprint(('image/png', b'same')), bridge.fingerprint((bridge.TEXT_TYPE, b'same')))
        self.assertEqual(bridge.fingerprint(('image/png', b'same')), bridge.fingerprint(('image/png', b'same')))

    def test_same_image_recopied_after_a_new_primary_refreshes_timestamp(self):
        value = (bridge.PNG_TYPE, bridge.PNG_SIGNATURE + b'image')
        selection = Mock()
        selection.owner_id.return_value = 123
        with patch.object(bridge, '_owner_window', 123), patch.object(bridge, '_owner_timestamp', 100), patch.object(bridge, 'run') as read:
            read.return_value = b'101\n'
            self.assertTrue(bridge.should_offer_x11(value, bridge.fingerprint(value), selection))
            read.return_value = b'99\n'
            self.assertFalse(bridge.should_offer_x11(value, bridge.fingerprint(value), selection))
            selection.owner_id.return_value = 456
            read.return_value = b'101\n'
            self.assertFalse(bridge.should_offer_x11(value, bridge.fingerprint(value), selection))
            self.assertTrue(bridge.should_offer_x11(value, None, selection))

    @unittest.skipIf(sys.platform == 'win32', 'Clipboard pipe transfers run on Linux')
    def test_binary_transfer_and_size_bound(self):
        data = bridge.run([sys.executable, '-c', 'import sys;sys.stdout.buffer.write(bytes(range(256))*8000)'], limit=3 << 20)
        self.assertEqual(data, bytes(range(256)) * 8000)
        self.assertIsNone(bridge.run([sys.executable, '-c', 'import sys;sys.stdout.buffer.write(b"x"*1000000)'], limit=100))
        self.assertIsNone(bridge.run([sys.executable, '-c', 'raise SystemExit(1)']))

    def test_png_and_text_offer_matching_targets(self):
        with patch.object(bridge, 'serve', return_value=True) as serve, patch.object(bridge, 'start_x11_owner', return_value=True) as owner:
            png = (bridge.PNG_TYPE, bridge.PNG_SIGNATURE + b'\x00')
            self.assertTrue(bridge.offer_x11(png))
            owner.assert_called_with(png)
            bridge.offer_wayland(png)
            self.assertEqual(serve.call_args.args[0], ['wl-copy', '--type', 'image/png'])
            bridge.offer_x11((bridge.TEXT_TYPE, b'text'))
            owner.assert_called_with((bridge.TEXT_TYPE, b'text'))
            files = (bridge.URI_TYPE, b'file:///tmp/archive.zip\r\n')
            self.assertTrue(bridge.offer_x11(files))
            owner.assert_called_with(files)
            bridge.offer_wayland(files)
            self.assertEqual(serve.call_args.args[0], ['wl-copy', '--type', 'text/uri-list'])
if __name__ == '__main__':
    unittest.main()
