# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned-source and archive proofs, using data fixtures rather than a runtime."""
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prepare_swift as swift


class SwiftInputs(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.snapshot = self.root / "snapshot"
        self.snapshot.mkdir()
        self.payloads = {"libswiftFirst.dylib": b"first Swift data", "libswiftSecond.dylib": b"second Swift data"}
        self.files = {}
        pointers = {}
        for name, data in self.payloads.items():
            digest = hashlib.sha256(data).hexdigest()
            pointer = f"version https://git-lfs.github.com/spec/v1\noid sha256:{digest}\nsize {len(data)}\n".encode()
            oid = hashlib.sha1(b"blob " + str(len(pointer)).encode() + b"\0" + pointer).hexdigest()
            self.files[name] = {"path": swift.SOURCE_PREFIX + name, "sha256": digest,
                                "bytes": len(data), "pointer_blob": oid, "materialized": False}
            pointers[swift.SOURCE_PREFIX + name] = pointer
        source = self.snapshot / "source.tar.gz"
        with tarfile.open(source, "w:gz") as tar:
            for name, data in pointers.items():
                member = tarfile.TarInfo(name)
                member.size = len(data)
                tar.addfile(member, io.BytesIO(data))
        self.manifest = {"schema": 1, "kind": "source-snapshot",
            "source": {"url": "https://github.com/darlinghq/darling.git", "revision": swift.ROOT_REVISION},
            "modules": [{"path": swift.SOURCE_PREFIX, "revision": swift.SWIFT_REVISION,
                         "url": "https://github.com/darlinghq/darling-swift.git"}],
            "lfs": list(self.files.values()), "archive": {"file": "source.tar.gz",
                "bytes": source.stat().st_size, "sha256": swift.digest(source)}}
        (self.snapshot / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")
        self.count = patch.object(swift, "EXPECTED_COUNT", len(self.files))
        self.count.start()
        self.addCleanup(self.count.stop)

    def tar(self, kind=None, duplicate=False, missing=False, changed=False):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w") as tar:
            for index, (name, data) in enumerate(self.payloads.items()):
                if missing and index == 1:
                    continue
                member = tarfile.TarInfo("./" + swift.ARCHIVE_PREFIX + name)
                member.size = len(data)
                if kind is not None:
                    member.type = kind
                    member.linkname = "/outside"
                    member.size = 0
                tar.addfile(member, io.BytesIO(b"X" * len(data) if changed else data))
                if duplicate:
                    tar.addfile(member, io.BytesIO(data))
        stream.seek(0)
        return stream

    def test_inventory_and_original_pointer_archive_agree(self):
        self.assertEqual(swift.inventory(self.manifest), self.files)
        swift.check_snapshot(self.snapshot, self.manifest, self.files)

    def test_wrong_revision_duplicate_materialized_and_missing_inventory_fail(self):
        for mutation in ("revision", "root", "duplicate", "materialized", "missing", "filename"):
            manifest = copy.deepcopy(self.manifest)
            if mutation == "revision": manifest["modules"][0]["revision"] = "0" * 40
            if mutation == "root": manifest["source"]["revision"] = "0" * 40
            if mutation == "duplicate": manifest["lfs"].append(manifest["lfs"][0])
            if mutation == "materialized": manifest["lfs"][0]["materialized"] = True
            if mutation == "missing": manifest["lfs"].pop()
            if mutation == "filename": manifest["lfs"][0]["path"] = swift.SOURCE_PREFIX + "../escape"
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                swift.inventory(manifest)

    def test_tampered_source_or_manifest_cannot_supply_replacement_hashes(self):
        files = copy.deepcopy(self.files)
        files["libswiftFirst.dylib"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "pinned source pointer"):
            swift.check_snapshot(self.snapshot, self.manifest, files)
        (self.snapshot / "source.tar.gz").write_bytes(b"changed source")
        with self.assertRaisesRegex(ValueError, "checksum/size"):
            swift.check_snapshot(self.snapshot, self.manifest, self.files)

    def test_regular_exact_payloads_only(self):
        stage = self.root / "stage"
        stage.mkdir()
        swift.recover_members(self.tar(), self.files, stage)
        for name, data in self.payloads.items():
            self.assertEqual((stage / name).read_bytes(), data)
        self.assertFalse((self.root / "outside").exists())

    def test_duplicate_missing_changed_symlink_hardlink_members_fail(self):
        for index, options in enumerate(({"duplicate": True}, {"missing": True}, {"changed": True},
                                          {"kind": tarfile.SYMTYPE}, {"kind": tarfile.LNKTYPE})):
            stage = self.root / str(index)
            stage.mkdir()
            with self.subTest(options=options), self.assertRaises(ValueError):
                swift.recover_members(self.tar(**options), self.files, stage)

    def test_untrusted_archive_fails_before_starting_dpkg(self):
        archive = self.root / "wrong.deb"
        archive.write_bytes(b"wrong archive")
        with patch.object(swift.subprocess, "Popen") as process:
            with self.assertRaisesRegex(ValueError, "Official recovery archive"):
                swift.prepare(self.snapshot, archive, self.root / "objects", self.root / "output")
            process.assert_not_called()
        self.assertFalse((self.root / "output").exists())

    @unittest.skipUnless(shutil.which("dpkg-deb"), "native Debian archive fixture")
    def test_real_deb_data_read_cache_reuse_and_corrupt_cache_refusal(self):
        package = self.root / "package"
        control = package / "DEBIAN"
        control.mkdir(parents=True)
        (control / "control").write_text("Package: karyo-swift-data-fixture\nVersion: 1\nArchitecture: all\nMaintainer: Fixture <fixture@openkaryon.invalid>\nDescription: Synthetic data fixture\n", encoding="utf-8")
        # This script must never execute: only dpkg-deb --fsys-tarfile is used.
        (control / "postinst").write_text("#!/bin/sh\nexit 99\n", encoding="utf-8")
        (control / "postinst").chmod(0o755)
        for name, data in self.payloads.items():
            path = package / swift.ARCHIVE_PREFIX / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        archive = self.root / "fixture.deb"
        subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(package), str(archive)], check=True, capture_output=True)
        with patch.object(swift, "ARCHIVE_SIZE", archive.stat().st_size), patch.object(swift, "ARCHIVE_SHA256", swift.digest(archive)):
            objects = self.root / "objects"
            first = swift.prepare(self.snapshot, archive, objects, self.root / "first")
            original = {p.name: (p.stat().st_mtime_ns, p.read_bytes()) for p in objects.iterdir()}
            swift.prepare(self.snapshot, archive, objects, self.root / "second")
            self.assertEqual(original, {p.name: (p.stat().st_mtime_ns, p.read_bytes()) for p in objects.iterdir()})
            self.assertFalse(first["native_linux_binaries"])
            self.assertFalse(first["compiled_from_source"])
            self.assertTrue((self.root / "first/swift-LICENSE.txt").is_file())
            next(objects.iterdir()).write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "corrupt"):
                swift.prepare(self.snapshot, archive, objects, self.root / "third")
            self.assertFalse((self.root / "third/provenance.json").exists())


if __name__ == "__main__":
    unittest.main()
