# SPDX-License-Identifier: GPL-3.0-or-later
"""Policy regressions without invoking account, PAM, session or network commands."""
import ipaddress
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.model import (DesktopAddress, DesktopSettings, MAX_CONFIG_BYTES, Ownership,
                                  account_name, label, root_file)


class OwnershipTest(unittest.TestCase):
    def test_legacy_lines_preserve_order_and_independent_families(self):
        mapping = Ownership.parse("# main then children\n danil work experiments # comment\n other project\n")
        self.assertEqual(mapping.families, (("danil", "work", "experiments"), ("other", "project")))
        self.assertEqual(mapping.owner("experiments"), "danil")
        self.assertTrue(mapping.is_child("work"))
        self.assertFalse(mapping.is_child("danil"))
        self.assertEqual(mapping.family("project"), ("other", "project"))
        self.assertEqual(mapping.family("outside"), ("outside",))
        self.assertEqual(Ownership.parse(mapping.serialize()), mapping)

    def test_child_creation_from_a_child_adds_only_a_sibling(self):
        mapping = Ownership.parse("danil work\nother project\n")
        result = mapping.with_child("work", "experiments")
        self.assertEqual(result.family("experiments"), ("danil", "work", "experiments"))
        self.assertEqual(result.owner("experiments"), "danil")
        self.assertEqual(mapping.family("work"), ("danil", "work"))
        self.assertEqual(result.family("project"), ("other", "project"))

    def test_unconfigured_account_can_create_its_own_family(self):
        mapping = Ownership.parse("other project\n").with_child("danil", "work")
        self.assertEqual(mapping.family("danil"), ("danil", "work"))

    def test_ambiguous_nested_and_duplicate_ownership_is_rejected(self):
        for value in ("danil danil", "danil work\nother work", "danil work\nwork nested"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                Ownership.parse(value)
        mapping = Ownership.parse("danil work\nother project")
        for caller, child in (("work", "work"), ("work", "danil"), ("work", "project"), ("outside", "outside")):
            with self.subTest(caller=caller, child=child), self.assertRaises(ValueError):
                mapping.with_child(caller, child)

    def test_unsafe_accounts_and_mutable_policy_are_rejected(self):
        for name in ("../user", "-option", "with space", "root\n", "a/b", "a:b", "a" * 33, "", "данил"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                account_name(name)
        self.assertEqual(account_name("a" * 32), "a" * 32)
        self.assertEqual(account_name("machine$"), "machine$")
        for rows in ([["danil", "work"]], (["danil", "work"],), ((),)):
            with self.assertRaises(ValueError):
                Ownership(rows)

    def test_bounded_utf8_and_nul_policy(self):
        for value in ("\0", "#" * (MAX_CONFIG_BYTES + 1), "#" + "я" * MAX_CONFIG_BYTES):
            with self.assertRaises(ValueError):
                Ownership.parse(value)


class SettingsTest(unittest.TestCase):
    def test_defaults_and_supported_legacy_quoted_values(self):
        self.assertEqual(DesktopSettings.parse("# default\n"), DesktopSettings())
        self.assertFalse(DesktopSettings().protected)
        settings = DesktopSettings.parse("PASSWORD='home'\nVPN=\"netbird\"\nWORKSTATION=yes # marker\n")
        self.assertTrue(settings.protected)
        self.assertEqual(settings.password, "home")
        self.assertEqual(settings.vpn, "netbird")
        self.assertTrue(settings.workstation)
        self.assertEqual(DesktopSettings.parse(settings.serialize()), settings)
        self.assertTrue(DesktopSettings(password="separate").protected)

    def test_unknown_duplicate_and_shell_values_are_not_configuration(self):
        for value in ("PASSWORD=home\nPASSWORD=none", "VPN=$(touch /tmp/no)", "export VPN=netbird",
                      "VPN=other", "PASSWORD=invalid", "WORKSTATION=1", "UNKNOWN=yes", "broken"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                DesktopSettings.parse(value)
        with self.assertRaises(ValueError):
            DesktopSettings(workstation=1)
        with self.assertRaises(ValueError):
            DesktopSettings(password="invalid")

    def test_labels_preserve_unicode_but_reject_record_and_display_controls(self):
        self.assertEqual(label("  Эксперименты  "), "Эксперименты")
        for value in ("", " \u00a0 ", "two\nlines", "x:y", "x,y", "\u202eSpoof", "x\u2028y", "a" * 81):
            with self.subTest(value=value), self.assertRaises(ValueError):
                label(value)


class AddressTest(unittest.TestCase):
    def test_distinct_subnets_and_exact_last_boundary(self):
        first = DesktopAddress.for_uid(1000)
        second = DesktopAddress.for_uid(1001)
        last = DesktopAddress.for_uid(17383)
        self.assertEqual(str(first.subnet), "10.231.0.0/30")
        self.assertEqual(str(first.host), "10.231.0.1")
        self.assertEqual(str(first.child), "10.231.0.2")
        self.assertEqual(first.interface, "kd1000")
        self.assertFalse(first.subnet.overlaps(second.subnet))
        self.assertEqual(str(last.child), "10.231.255.254")
        with self.assertRaises(ValueError):
            DesktopAddress.for_uid(17384)

    def test_invalid_uid_and_non_private_pools_are_rejected(self):
        for uid in (True, "1001", 999, 65534, -1):
            with self.subTest(uid=uid), self.assertRaises(ValueError):
                DesktopAddress.for_uid(uid)
        for pool in ("10.0.0.0/16", ipaddress.IPv4Network("127.0.0.0/8"),
                     ipaddress.IPv4Network("1.1.1.0/24"), ipaddress.IPv4Network("10.0.0.0/31"),
                     ipaddress.IPv6Network("fd00::/64")):
            with self.subTest(pool=pool), self.assertRaises(ValueError):
                DesktopAddress.for_uid(1000, pool)
        self.assertEqual(str(DesktopAddress.for_uid(1001, ipaddress.IPv4Network("192.168.8.0/24")).child),
                         "192.168.8.6")


class RootFileTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.current = self.root / "current.conf"
        self.legacy = self.root / "legacy.conf"

    def test_missing_maps_are_empty(self):
        self.assertEqual(Ownership.load(self.current, self.legacy), Ownership())

    def test_unsafe_types_permissions_and_ownership_are_refused(self):
        target = self.root / "target.conf"
        target.write_text("danil work\n")
        os.chmod(target, 0o666)
        with self.assertRaises(ValueError):
            root_file(target)
        os.chmod(target, 0o644)
        if os.getuid() != 0:
            with self.assertRaisesRegex(ValueError, "root-owned"):
                root_file(target)
        os.symlink(target, self.current)
        with self.assertRaises(OSError):
            Ownership.load(self.current, self.legacy)
        fifo = self.root / "fifo"
        os.mkfifo(fifo)
        with self.assertRaises(ValueError):
            root_file(fifo)

    @unittest.skipUnless(os.getuid() == 0, "native root-owned file fixtures")
    def test_current_file_precedes_legacy_and_invalid_current_never_falls_back(self):
        self.legacy.write_text("legacy child\n")
        self.current.write_text("danil work\n")
        os.chmod(self.legacy, 0o644)
        os.chmod(self.current, 0o644)
        self.assertEqual(Ownership.load(self.current, self.legacy).owner("work"), "danil")
        self.current.write_text("danil work\nother work\n")
        with self.assertRaises(ValueError):
            Ownership.load(self.current, self.legacy)
        self.current.write_text("")
        self.assertEqual(Ownership.load(self.current, self.legacy), Ownership())
        self.current.unlink()
        self.assertEqual(Ownership.load(self.current, self.legacy).owner("child"), "legacy")

    @unittest.skipUnless(os.getuid() == 0, "native root-owned file fixtures")
    def test_large_invalid_utf8_and_directory_are_not_ownership_files(self):
        self.current.write_bytes(b"x" * (MAX_CONFIG_BYTES + 1))
        with self.assertRaises(ValueError):
            root_file(self.current)
        self.current.write_bytes(b"\xff")
        with self.assertRaises(UnicodeError):
            root_file(self.current)
        with self.assertRaises(ValueError):
            root_file(self.root)


if __name__ == "__main__":
    unittest.main()
