# SPDX-License-Identifier: GPL-3.0-or-later
"""Use native dpkg-source to test the exact archive/overlay format we emit."""
from email.parser import Parser
import gzip
import hashlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prepare_source as source


class DebianSource(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.packaging = Path(__file__).resolve().parents[1] / "packaging"

    def files(self):
        return {"debian/control": (self.packaging / "control").read_bytes(),
                "debian/rules": (self.packaging / "rules").read_bytes(),
                "debian/source/format": b"3.0 (quilt)\n",
                "debian/changelog": (source.NAME + " (" + source.VERSION + ") unstable; urgency=medium\n\n"
                    "  * Source format fixture.\n\n -- " + source.MAINTAINER + "  Tue, 14 Nov 2023 22:13:20 +0000\n").encode()}

    def test_overlay_archive_is_repeatable_and_preserves_rules_mode(self):
        a, b = self.root / "first.tar.gz", self.root / "second.tar.gz"
        source.write_overlay(a, self.files(), 1700000000)
        source.write_overlay(b, self.files(), 1700000000)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        with tarfile.open(a) as tar:
            self.assertEqual(tar.getmember("debian/rules").mode, 0o755)
            self.assertEqual(tar.getmember("debian/control").mode, 0o644)
            self.assertTrue(all(m.uid == 0 and m.gid == 0 and m.mtime == 1700000000 for m in tar))

    def test_dsc_has_exact_native_identity_and_all_strong_checksums(self):
        first, second = self.root / "one.tar.gz", self.root / "two.tar.gz"
        first.write_bytes(b"first input")
        second.write_bytes(b"second input")
        headers = Parser().parsestr(source.source_control((first, second), self.files()["debian/control"].decode()))
        self.assertEqual(headers["Format"], "3.0 (quilt)")
        self.assertEqual(headers["Source"], source.NAME)
        self.assertEqual(headers["Version"], source.VERSION)
        values = [line.split() for line in headers["Checksums-Sha256"].splitlines() if line.strip()]
        self.assertEqual(values, [[hashlib.sha256(p.read_bytes()).hexdigest(), str(p.stat().st_size), p.name] for p in (first, second)])

    def test_unverified_or_stale_native_patch_receipt_fails_before_output(self):
        snapshot, swift = self.root / "snapshot", self.root / "swift"
        snapshot.mkdir(); swift.mkdir()
        receipt = self.root / "patch-verification.json"
        for record in ({"status": "verified"},
                       {"status": "verified", "quilt_zero_fuzz": True, "archive": {}, "source": {}, "checker_sha256": "stale"}):
            import json
            receipt.write_text(json.dumps(record))
            with patch.object(source, "check_inputs", return_value=({"archive": {}, "source": {}}, {}, [])):
                with self.assertRaisesRegex(ValueError, "native zero-fuzz quilt"):
                    source.prepare(snapshot, swift, receipt, self.root / "output")
            self.assertFalse((self.root / "output").exists())

    @unittest.skipUnless(shutil.which("dpkg-source"), "native source extractor")
    def test_native_extraction_handles_no_root_prefix_and_replaces_upstream_debian(self):
        original = self.root / (source.NAME + "_" + source.VERSION.rsplit("-", 1)[0] + ".orig.tar.gz")
        payloads = {"CMakeLists.txt": b"# original source\n", "Case": b"upper", "case": b"lower",
                    "debian/postinst": b"#!/bin/sh\nexit 99\n", "debian/old-data": b"obsolete packaging"}
        source.write_overlay(original, payloads, 1700000000)
        # Include an explicit external source symlink as inert archive data.
        with tarfile.open(original, "r:gz") as tar:
            members = [(m, tar.extractfile(m).read()) for m in tar]
        with tarfile.open(original, "w:gz") as tar:
            for member, data in members:
                tar.addfile(member, io.BytesIO(data))
            member = tarfile.TarInfo("Developer/usr/bin/git")
            member.type, member.linkname = tarfile.SYMTYPE, "/usr/bin/git"
            tar.addfile(member)
        overlay = self.root / (source.NAME + "_" + source.VERSION + ".debian.tar.gz")
        source.write_overlay(overlay, self.files(), 1700000000)
        dsc = self.root / (source.NAME + "_" + source.VERSION + ".dsc")
        dsc.write_text(source.source_control((original, overlay), self.files()["debian/control"].decode()))
        output = self.root / "unpacked"
        result = subprocess.run(["dpkg-source", "--require-strong-checksums", "-x", str(dsc), str(output)],
                                cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((output / "CMakeLists.txt").read_bytes(), payloads["CMakeLists.txt"])
        self.assertEqual((output / "Case").read_bytes(), b"upper")
        self.assertEqual((output / "case").read_bytes(), b"lower")
        self.assertEqual((output / "debian/control").read_bytes(), self.files()["debian/control"])
        self.assertFalse((output / "debian/postinst").exists())
        self.assertFalse((output / "debian/old-data").exists())
        self.assertEqual((output / "Developer/usr/bin/git").readlink().as_posix(), "/usr/bin/git")
