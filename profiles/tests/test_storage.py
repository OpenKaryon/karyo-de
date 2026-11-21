# SPDX-License-Identifier: GPL-3.0-or-later
"""Real descriptor, atomic replacement and interprocess locking checks."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.model import DesktopSettings, Ownership
from karyo_profiles.storage import ConfigStore, trusted_directory


@unittest.skipUnless(os.geteuid() == 0, "root-only storage lifecycle fixtures")
class StoreTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir="/root")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config = self.root / "etc/karyo"
        self.legacy = self.root / "etc/legacy"
        self.runtime = self.root / "run/karyo"
        self.store = ConfigStore(self.config, self.legacy, self.runtime)

    def test_writes_need_a_root_lock_and_roundtrip_public_metadata(self):
        mapping = Ownership.parse("danil work experiments\n")
        with self.assertRaises(PermissionError):
            self.store.save_ownership(mapping)
        self.assertFalse(self.config.exists())
        with self.store.locked():
            self.store.save_ownership(mapping)
            self.store.save_settings("experiments", DesktopSettings(password="separate"))
            with self.assertRaises(RuntimeError):
                with self.store.locked():
                    pass
        self.assertEqual(self.store.ownership(), mapping)
        self.assertEqual(self.store.settings("experiments"), DesktopSettings(password="separate"))
        self.assertEqual((self.config / "desktops.conf").stat().st_mode & 0o777, 0o644)
        self.assertEqual((self.runtime / "desktops.lock").stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.store.settings("missing"), DesktopSettings())

    def test_restrictive_umask_does_not_hide_new_public_metadata(self):
        old = os.umask(0o077)
        try:
            with self.store.locked():
                self.store.save_settings("work", DesktopSettings(vpn="netbird"))
        finally:
            os.umask(old)
        self.assertEqual(self.config.stat().st_mode & 0o777, 0o755)
        self.assertEqual((self.config / "desktops.d").stat().st_mode & 0o777, 0o755)
        self.assertEqual((self.config / "desktops.d/work.conf").stat().st_mode & 0o777, 0o644)

    def test_failed_publication_preserves_prior_bytes_and_removes_temporary(self):
        before = Ownership.parse("danil work\n")
        after = before.with_child("work", "experiments")
        with self.store.locked():
            self.store.save_ownership(before)
            with patch("karyo_profiles.storage.os.replace", side_effect=OSError("fixture rename failure")):
                with self.assertRaises(OSError):
                    self.store.save_ownership(after)
            self.assertEqual(self.store.ownership(), before)
            self.assertFalse(list(self.config.glob(".karyo-*.tmp")))

    def test_failed_write_does_not_publish_partial_configuration(self):
        before = Ownership.parse("danil work\n")
        with self.store.locked():
            self.store.save_ownership(before)
            with patch("karyo_profiles.storage.os.write", return_value=0):
                with self.assertRaises(OSError):
                    self.store.save_ownership(before.with_child("work", "experiments"))
            self.assertEqual(self.store.ownership(), before)
            self.assertFalse(list(self.config.glob(".karyo-*.tmp")))

    def test_symlink_fifo_and_hardlinked_destinations_are_not_replaced(self):
        with self.store.locked():
            self.store.save_settings("work", DesktopSettings())
            target = self.config / "desktops.d/work.conf"
            original = target.read_bytes()
            link = self.config / "desktops.d/link.conf"
            os.symlink(target, link)
            with self.assertRaises(OSError):
                self.store.save_settings("link", DesktopSettings(password="home"))
            fifo = self.config / "desktops.d/fifo.conf"
            os.mkfifo(fifo)
            with self.assertRaises(ValueError):
                self.store.save_settings("fifo", DesktopSettings())
            hard = self.config / "desktops.d/hard.conf"
            os.link(target, hard)
            with self.assertRaises(ValueError):
                self.store.save_settings("hard", DesktopSettings())
            self.assertEqual(target.read_bytes(), original)
            self.assertTrue(link.is_symlink())

    def test_untrusted_or_symlinked_parent_is_not_used(self):
        self.legacy.mkdir(parents=True)
        (self.legacy / "desktops.conf").write_text("legacy child\n")
        self.config.mkdir()
        os.chmod(self.config, 0o777)
        with self.assertRaises(ValueError):
            self.store.ownership()
        with self.store.locked(), self.assertRaises(ValueError):
            self.store.save_ownership(Ownership())
        os.chmod(self.config, 0o755)
        self.config.rmdir()
        os.symlink(self.legacy, self.config)
        with self.assertRaises(OSError):
            self.store.ownership()
        with self.assertRaises(ValueError):
            trusted_directory(self.root / "etc/../escape")

    def test_invalid_current_prevents_legacy_fallback(self):
        self.legacy.mkdir(parents=True)
        (self.legacy / "desktops.conf").write_text("legacy child\n")
        self.assertEqual(self.store.ownership().owner("child"), "legacy")
        with self.store.locked():
            self.store.save_ownership(Ownership.parse("danil work\n"))
        (self.config / "desktops.conf").write_text("danil work\nother work\n")
        with self.assertRaises(ValueError):
            self.store.ownership()

    def test_lock_excludes_an_independent_process(self):
        code = """import fcntl,os,sys
fd=os.open(sys.argv[1],os.O_RDWR)
try:
 fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
except BlockingIOError:
 sys.exit(0)
sys.exit(1)
"""
        with self.store.locked():
            result = subprocess.run([sys.executable, "-I", "-c", code, str(self.runtime / "desktops.lock")],
                                    timeout=5, check=False)
            self.assertEqual(result.returncode, 0)
        # A failed operation must also release its descriptor lock.
        with self.assertRaises(RuntimeError):
            with self.store.locked():
                raise RuntimeError("fixture")
        with self.store.locked():
            pass

    def test_busy_lock_has_a_bounded_failure_and_releases_resources(self):
        with patch("karyo_profiles.storage.fcntl.flock", side_effect=BlockingIOError):
            with self.assertRaises(TimeoutError):
                with self.store.locked(timeout=0):
                    self.fail("Busy lock cannot enter the operation")
        with self.store.locked():
            pass
        for timeout in (True, -1, 31, float("nan")):
            with self.assertRaises(ValueError):
                with self.store.locked(timeout=timeout):
                    pass

    def test_unsafe_lock_cannot_be_followed_or_shared(self):
        self.runtime.mkdir(parents=True)
        target = self.root / "target"
        target.write_text("preserved")
        os.symlink(target, self.runtime / "desktops.lock")
        with self.assertRaises(OSError):
            with self.store.locked():
                pass
        self.assertEqual(target.read_text(), "preserved")


class UnprivilegedStoreTest(unittest.TestCase):
    @unittest.skipIf(os.geteuid() == 0, "run separately as an ordinary user")
    def test_unprivileged_lock_fails_before_creating_any_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            store = ConfigStore(path / "etc", path / "legacy", path / "run")
            with self.assertRaises(PermissionError):
                with store.locked():
                    pass
            self.assertEqual(list(path.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
