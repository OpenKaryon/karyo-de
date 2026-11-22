# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic wrapper-only inputs; these do not establish Darling compatibility."""
from pathlib import Path
import os
import struct
import sys
import tempfile
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_macos import applications, profiles


def macho(cpu=0x1000007, kind=2):
    return struct.pack('<8I', 0xFEEDFACF, cpu, 3, kind, 0, 0, 0, 0)


class ProfileFixture:
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='km-')
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.home = self.folder / 'h'
        self.home.mkdir()
        self.uid = os.getuid()
        self.account = patch.object(profiles, 'normal_user', return_value=(self.uid, self.home))
        self.account.start(); self.addCleanup(self.account.stop)
        self.mount = patch.object(profiles, 'mount_filesystem_type', return_value='ext4')
        self.mount.start(); self.addCleanup(self.mount.stop)
        self.engine = patch.object(profiles.shutil, 'which', return_value='/test/darling')
        self.engine.start(); self.addCleanup(self.engine.stop)
        self.binary = self.folder / "Пример $(literal) app"
        self.binary.write_bytes(macho()); self.binary.chmod(0o755)
        self.app = applications.inspect_application(self.binary)

    def plan(self, environment=None, arguments=None, create=True):
        return profiles.prepare_launch(self.app, arguments or [],
            environment={'DISPLAY': ':1', 'XAUTHORITY': '/private/auth'} if environment is None else environment,
            create=create)
