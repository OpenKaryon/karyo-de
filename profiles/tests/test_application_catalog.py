# SPDX-License-Identifier: GPL-3.0-or-later
import json
import multiprocessing
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles import application_catalog as catalog
from karyo_profiles.model import DesktopSettings, Ownership

SAMPLE = {"version": 1, "applications": {"home.desktop": ["home"], "work.desktop": ["work"],
          "common.desktop": ["home", "work"]}, "autostart": {"work.desktop": ["work"], "manual.desktop": []}}


class Store:
    def __init__(self, work=True):
        self.work = work

    def ownership(self):
        return Ownership.parse("main desk experiments\nother separate\n")

    def settings(self, name):
        return DesktopSettings(workstation=self.work and name == "desk")


def concurrent_publish(arguments):
    runtime, plan = arguments
    return str(catalog.publish(runtime, plan))


class RolePolicyTest(unittest.TestCase):
    def test_roles_use_family_metadata_and_preserve_unrelated_accounts(self):
        store = Store()
        self.assertEqual(catalog.role_for(store, "desk"), "work")
        self.assertEqual(catalog.role_for(store, "main"), "home")
        self.assertEqual(catalog.role_for(store, "experiments"), "home")
        self.assertIsNone(catalog.role_for(store, "other"))
        self.assertIsNone(catalog.role_for(Store(False), "main"))

    def test_common_apps_and_role_autostart_have_independent_policy(self):
        policy = catalog.parse(json.dumps(SAMPLE))
        self.assertEqual(catalog.view(policy, "home"),
                         {"role": "home", "applications": ["work.desktop"], "autostart": ["manual.desktop", "work.desktop"]})
        self.assertEqual(catalog.view(policy, "work"),
                         {"role": "work", "applications": ["home.desktop"], "autostart": ["manual.desktop"]})

    def test_invalid_or_command_like_policy_never_becomes_a_view(self):
        for change in ({"version": True}, {"version": 2}, {"extra": "command"},
                       {"applications": {"../escape.desktop": ["home"]}},
                       {"applications": {"bad.desktop": ["admin"]}},
                       {"applications": {"bad.desktop": ["home", "home"]}},
                       {"applications": {"bad.desktop": [["home"]]}},
                       {"applications": {"bad.desktop": "home"}},
                       {"autostart": {"bad.desktop": [True]}},
                       {"applications": {"a"*i+".desktop": [] for i in range(257)}}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                catalog.parse(json.dumps(dict(SAMPLE, **change)))
        with self.assertRaises(ValueError):
            catalog.parse('{"version":1,"version":1,"applications":{},"autostart":{}}')

    def test_no_metadata_is_inert_and_root_cli_is_not_an_administrator_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            from unittest.mock import patch
            if os.geteuid() == 0 or os.getuid() >= 65534:
                with self.assertRaises(PermissionError):
                    catalog.apply(policy_path=Path(directory)/"missing")
            # No runtime or account metadata is touched when an ordinary user
            # has no installed optional role policy.
            with patch.object(catalog.os, "getuid", return_value=1002), \
                    patch.object(catalog.os, "geteuid", return_value=1002), \
                    patch.object(catalog, "load", return_value=None), \
                    patch.object(catalog.pwd, "getpwuid") as account:
                self.assertIsNone(catalog.apply())
                account.assert_not_called()


@unittest.skipUnless(os.geteuid() == 0, "root-owned optional role metadata")
class RootMetadataTest(unittest.TestCase):
    def test_read_only_trusted_policy_and_missing_policy(self):
        with tempfile.TemporaryDirectory(dir="/root") as directory:
            root = Path(directory);path = root/"roles.json"
            self.assertIsNone(catalog.load(path))
            path.write_text(json.dumps(SAMPLE))
            self.assertEqual(catalog.load(path), SAMPLE)
            path.chmod(0o666)
            with self.assertRaises(ValueError):catalog.load(path)
            path.chmod(0o644);path.unlink();path.symlink_to(root/"other")
            with self.assertRaises(OSError):catalog.load(path)
            path.unlink();path.write_text(json.dumps(SAMPLE));root.chmod(0o775)
            with self.assertRaises(ValueError):catalog.load(path)


class CacheTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.runtime = Path(self.temporary.name)
        self.plan = catalog.view(SAMPLE, "home")

    def test_atomic_view_reuses_exact_content_without_copying_programs(self):
        path = catalog.publish(self.runtime, self.plan)
        before = {str(p.relative_to(path)): (p.stat().st_ino, p.read_bytes())
                  for p in path.rglob("*") if p.is_file()}
        self.assertEqual(catalog.publish(self.runtime, self.plan), path)
        self.assertEqual({str(p.relative_to(path)): (p.stat().st_ino,p.read_bytes())
                          for p in path.rglob("*") if p.is_file()}, before)
        self.assertEqual(set(before), {"share/applications/work.desktop", "config/autostart/manual.desktop",
                                      "config/autostart/work.desktop"})
        self.assertTrue(all(b"Exec=" not in data and b"Hidden=true" in data for unused,data in before.values()))
        work = catalog.publish(self.runtime, catalog.view(SAMPLE, "work"))
        self.assertNotEqual(work, path)
        catalog.check_cache(path, self.plan)
        self.assertFalse(list(self.runtime.glob(".karyo-applications-*")))

    def test_changed_cache_is_retained_instead_of_overwritten(self):
        path = catalog.publish(self.runtime, self.plan)
        unexpected = path/"user-data";unexpected.write_bytes(b"retain")
        with self.assertRaises(ValueError):catalog.publish(self.runtime, self.plan)
        self.assertEqual(unexpected.read_bytes(), b"retain")
        unexpected.unlink()
        entry = path/"share/applications/work.desktop";entry.write_bytes(b"changed")
        with self.assertRaises(ValueError):catalog.publish(self.runtime, self.plan)
        self.assertEqual(entry.read_bytes(), b"changed")

    def test_symlink_fifo_hardlink_and_unsafe_runtime_are_not_adopted(self):
        path = catalog.publish(self.runtime, self.plan)
        entry = path/"share/applications/work.desktop";entry.unlink()
        source = self.runtime/"foreign";source.write_bytes(b"retain")
        entry.symlink_to(source)
        with self.assertRaises(OSError):catalog.publish(self.runtime, self.plan)
        self.assertEqual(source.read_bytes(), b"retain")
        entry.unlink();os.mkfifo(entry)
        with self.assertRaises(ValueError):catalog.publish(self.runtime, self.plan)
        entry.unlink();os.link(source, entry)
        with self.assertRaises(ValueError):catalog.publish(self.runtime, self.plan)
        self.runtime.chmod(0o755)
        with self.assertRaises(PermissionError):catalog.publish(self.runtime, self.plan)

    def test_concurrent_sessions_share_only_the_complete_identical_view(self):
        with multiprocessing.get_context("fork").Pool(2) as workers:
            results = workers.map(concurrent_publish, [(self.runtime, self.plan)]*2)
        self.assertEqual(results[0], results[1])
        catalog.check_cache(Path(results[0]), self.plan)
        self.assertFalse(list(self.runtime.glob(".karyo-applications-*")))
