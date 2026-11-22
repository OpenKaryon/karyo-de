# SPDX-License-Identifier: GPL-3.0-or-later
"""Real file-format rejection; no foreign executable is run."""
import os
import plistlib
import struct
import unittest
from support import ProfileFixture, applications, macho
from karyo_macos.errors import LaunchError


class Applications(ProfileFixture, unittest.TestCase):
    def bundle(self, format=plistlib.FMT_XML, name='binary'):
        bundle = self.folder / 'Тест.app'
        binaries = bundle / 'Contents/MacOS'
        binaries.mkdir(parents=True)
        (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleExecutable': name, 'CFBundlePackageType': 'APPL', 'CFBundleDisplayName': 'Пример'}, fmt=format))
        if '/' not in name:
            (binaries / name).write_bytes(macho())
        return bundle

    def test_thin_and_both_fat_tables(self):
        self.assertFalse(applications.inspect_application(self.binary).bundle)
        for wide in (False, True):
            offset = 64
            table = struct.pack('>IIQQII', 0x1000007, 3, offset, 32, 0, 0) if wide else struct.pack('>IIIII', 0x1000007, 3, offset, 32, 0)
            header = struct.pack('>II', 0xCAFEBABF if wide else 0xCAFEBABE, 1) + table
            self.binary.write_bytes(header.ljust(offset, b'\0') + macho())
            self.assertEqual(applications.inspect_application(self.binary).executable, self.binary)
            self.binary.write_bytes(header + macho())
            with self.assertRaises(LaunchError): applications.inspect_application(self.binary)

    def test_xml_and_binary_bundle_metadata(self):
        for format in (plistlib.FMT_XML, plistlib.FMT_BINARY):
            bundle = self.bundle(format)
            app = applications.inspect_application(bundle)
            self.assertTrue(app.bundle); self.assertEqual(app.name, 'Пример')
            import shutil
            shutil.rmtree(bundle)

    def test_arm_dylib_foreign_truncated_and_absent_intel_slice(self):
        for value in (macho(0x100000C), macho(kind=6), b'MZ'+b'\0'*30, b'\x7fELF'+b'\0'*28,
                      b'\xcf\xfa\xed\xfe', struct.pack('>II', 0xCAFEBABE, 65),
                      struct.pack('>II', 0xCAFEBABE, 1)+struct.pack('>IIIII', 0x100000C, 3, 32, 32, 0)+macho()):
            self.binary.write_bytes(value)
            with self.subTest(header=value[:8]), self.assertRaises(LaunchError): applications.inspect_application(self.binary)

    def test_bundle_escape_and_wrong_package_type(self):
        bundle = self.bundle(name='../outside')
        with self.assertRaises(LaunchError): applications.inspect_application(bundle)
        metadata = bundle / 'Contents/Info.plist'
        metadata.write_bytes(plistlib.dumps({'CFBundleExecutable': 'outside', 'CFBundlePackageType': 'BNDL'}))
        with self.assertRaises(LaunchError): applications.inspect_application(bundle)
        metadata.write_bytes(plistlib.dumps({'CFBundleExecutable': 'outside'}))
        (bundle / 'Contents/MacOS/outside').symlink_to(self.binary)
        with self.assertRaisesRegex(LaunchError, 'пределы'): applications.inspect_application(bundle)

    def test_invalid_nonregular_and_oversized_metadata(self):
        bundle = self.bundle(); metadata = bundle / 'Contents/Info.plist'
        for value in (b'not plist', plistlib.dumps(['not dictionary']), b'x'*(1024*1024+1)):
            metadata.write_bytes(value)
            with self.assertRaises(LaunchError): applications.inspect_application(bundle)
        metadata.unlink(); os.mkfifo(metadata)
        with self.assertRaises(LaunchError): applications.inspect_application(bundle)

    def test_fifo_executable_cannot_block_inspection(self):
        self.binary.unlink(); os.mkfifo(self.binary)
        with self.assertRaises(LaunchError): applications.inspect_application(self.binary)

    def test_installers_and_arbitrary_directories_are_rejected(self):
        for name in ('Installer.pkg', 'Image.dmg'):
            path = self.folder / name; path.write_bytes(macho())
            with self.assertRaisesRegex(LaunchError, 'пока не поддерживаются'): applications.inspect_application(path)
        with self.assertRaises(LaunchError): applications.inspect_application(self.home)
