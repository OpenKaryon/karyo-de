# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail before dependency tools if a staged payload has unsafe permissions."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("karyo_native_audit", Path(__file__).resolve().parents[1] / "packaging/native_audit.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class NativeAuditBoundary(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.build = self.source / "obj-karyo"
        self.stage = self.source / "debian/karyo-darling-runtime"
        self.build.mkdir(parents=True)
        self.stage.mkdir(parents=True)

    def test_external_stage_or_missing_privileged_helper_fails_before_commands(self):
        outside = self.root / "outside"
        outside.mkdir()
        with patch.object(audit, "run") as run:
            with self.assertRaisesRegex(ValueError, "source-local"):
                audit.audit(outside, self.source, self.build)
            with self.assertRaisesRegex(ValueError, "SUID bootstrap"):
                audit.audit(self.stage, self.source, self.build)
            run.assert_not_called()

    @unittest.skipIf(os.name == "nt", "native Unix permission fixture")
    def test_extra_suid_or_writable_files_fail_before_dependency_commands(self):
        required = ["usr/bin/darling", "usr/bin/darlingserver", "usr/libexec/darling/usr/lib/native/libX11.dylib",
            "usr/libexec/darling/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"]
        required += [f"usr/libexec/darling/System/Library/Frameworks/{name}.framework/Versions/C/Resources/Backends/X11.backend/Contents/MacOS/X11"
                     for name in ("AppKit", "CoreGraphics")]
        for name in required:
            path = self.stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"data-only permission fixture")
            path.chmod(0o644)
        (self.stage / "usr/bin/darling").chmod(0o4755)
        for mode in (0o4755, 0o2755, 0o0664, 0o0666):
            forbidden = self.stage / "usr/bin/other"
            forbidden.write_bytes(b"unexpected payload")
            forbidden.chmod(mode)
            with self.subTest(mode=oct(mode)), patch.object(audit, "run") as run:
                with self.assertRaisesRegex(ValueError, "privileged|writable"):
                    audit.audit(self.stage, self.source, self.build)
                run.assert_not_called()
            forbidden.unlink()
