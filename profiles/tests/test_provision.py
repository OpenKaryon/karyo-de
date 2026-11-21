# SPDX-License-Identifier: GPL-3.0-or-later
"""Provisioning policy and failure ordering; no system account commands here."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.model import DesktopSettings, Ownership
from karyo_profiles.provision import AccountProvisioner, CreationError, PendingAccount
from karyo_profiles.storage import ConfigStore


class FixtureAccounts:
    def __init__(self):
        self.users = {name: LocalAccount(name, uid, uid, "/home/" + name, "/bin/bash")
                      for name, uid in (("main", 1000), ("work", 1001), ("other", 2000))}
    def by_uid(self, uid):
        if type(uid) is not int or not 1000 <= uid < 65534:
            raise ValueError("Not an ordinary user")
        return next(user for user in self.users.values() if user.uid == uid)
    def account(self, name, missing_ok=False):
        if name in self.users:
            return self.users[name]
        if missing_ok:
            return None
        raise ValueError("Missing fixture account")
    def password_hash(self, name):
        self.account(name)
        return "!locked-fixture-owner"


class FixtureBackend:
    def __init__(self, store):
        self.store = store
        self.accounts = FixtureAccounts()
        self.existing_homes = set()
        self.events = []
        self.expired = set()
        self.partial_create = False
        self.activation_failure = False
        self.activation_committed = False
        self.processes = False
        self.next_uid = 1002

    def names(self):
        return set(self.accounts.users) | {"reserved"}
    def home_exists(self, name):
        return name in self.existing_homes
    def inherited_groups(self, owner):
        self.events.append(("groups-from", owner.name))
        return ("audio", "sudo") if owner.name == "main" else ()
    def create(self, name, caption, groups):
        self.events.append(("create", name, caption, groups))
        user = LocalAccount(name, self.next_uid, self.next_uid, "/home/" + name, "/bin/bash")
        self.accounts.users[name] = user
        self.existing_homes.add(name)
        self.expired.add(name)
        if self.partial_create:
            raise OSError("fixture partial useradd")
        return PendingAccount(user, 1, 42)
    def has_processes(self, uid):
        return self.processes
    def can_rollback(self, pending):
        return pending.account.name in self.expired and not self.processes
    def remove_pending(self, pending):
        self.events.append(("remove", pending.account.name))
        self.accounts.users.pop(pending.account.name)
        self.existing_homes.remove(pending.account.name)
        self.expired.discard(pending.account.name)
    def activate(self, pending, settings, password, owner_hash):
        name = pending.account.name
        # Activation must follow both complete authoritative metadata writes.
        assert self.store.ownership().is_child(name)
        assert self.store.settings(name) == settings
        self.events.append(("activate", name, settings.password))
        if self.activation_committed:
            self.expired.remove(name)
        if self.activation_failure:
            raise OSError("fixture activation failure")
        self.expired.discard(name)


@unittest.skipUnless(os.geteuid() == 0, "root-owned provisioning metadata fixtures")
class ProvisionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir="/root")
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.store = ConfigStore(root / "etc/karyo", root / "etc/legacy", root / "run")
        with self.store.locked():
            self.store.save_ownership(Ownership.parse("main work\nother\n"))
        self.backend = FixtureBackend(self.store)
        self.provision = AccountProvisioner(self.store, self.backend)
        self.before = self.store.ownership()

    def test_child_creates_a_sibling_and_inherits_only_main_policy(self):
        result = self.provision.add(1001, "Эксперименты", name="experiments", protection="home")
        self.assertEqual(result.name, "experiments")
        self.assertEqual(self.store.ownership().family("work"), ("main", "work", "experiments"))
        self.assertEqual(self.store.settings("experiments"), DesktopSettings(password="home"))
        self.assertIn(("groups-from", "main"), self.backend.events)
        self.assertEqual(self.backend.events[-1], ("activate", "experiments", "home"))
        self.assertNotIn("experiments", self.backend.expired)

    def test_separate_password_is_never_written_to_profile_metadata(self):
        secret = "public-fixture-separate-password"
        self.provision.add(1000, "Experiments", name="experiments", protection="separate", password=secret)
        for path in self.store.configuration.rglob("*.conf"):
            self.assertNotIn(secret, path.read_text())
        self.assertEqual(self.store.settings("experiments").password, "separate")

    def test_another_person_creates_only_its_own_family(self):
        self.provision.add(2000, "Project", name="project")
        self.assertEqual(self.store.ownership().family("project"), ("other", "project"))
        self.assertEqual(self.store.ownership().family("main"), ("main", "work"))
        self.assertIn(("groups-from", "other"), self.backend.events)

    def test_reserved_accounts_groups_homes_and_metadata_are_not_reused(self):
        self.existing = "public existing directory data"
        self.backend.existing_homes.add("old-home")
        with self.store.locked():
            self.store.save_settings("old-config", DesktopSettings(password="separate"))
        for name in ("main", "reserved", "old-home", "old-config", "../bad", "-option"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.provision.add(1000, "Project", name=name)
        self.assertFalse(any(item[0] == "create" for item in self.backend.events))
        self.assertIn("old-home", self.backend.existing_homes)
        self.assertEqual(self.store.settings("old-config").password, "separate")

    def test_generated_name_skips_existing_directory_without_deleting_it(self):
        self.backend.existing_homes.add("work-2")
        result = self.provision.add(1000, "Work")
        self.assertEqual(result.name, "work-3")
        self.assertIn("work-2", self.backend.existing_homes)

    def test_invalid_password_and_caller_do_not_create_accounts(self):
        for password in ("", "two\nlines", "\0", "x" * 1025):
            with self.subTest(password=password), self.assertRaises(ValueError):
                self.provision.add(1000, "Project", name="project", protection="separate", password=password)
        with self.assertRaises(ValueError):
            self.provision.add(1000, "Project", password="unexpected")
        for uid in (0, True, 65534):
            with self.assertRaises(ValueError):
                self.provision.add(uid, "Project")
        self.assertFalse(any(item[0] == "create" for item in self.backend.events))

    def test_failure_before_map_publication_removes_only_new_expired_account(self):
        with patch.object(self.store, "save_ownership", side_effect=OSError("fixture")):
            with self.assertRaises(CreationError) as error:
                self.provision.add(1000, "Project", name="project")
        self.assertFalse(error.exception.retained)
        self.assertNotIn("project", self.backend.accounts.users)
        self.assertFalse(self.store.has_settings("project"))
        self.assertEqual(self.store.ownership(), self.before)
        self.assertEqual(set(self.backend.accounts.users), {"main", "work", "other"})

    def test_failure_after_map_rename_restores_family_before_removing_new_account(self):
        original = self.store.save_ownership
        calls = 0
        def fail_after_first_write(mapping):
            nonlocal calls
            calls += 1
            original(mapping)
            if calls == 1:
                raise OSError("fixture durability failure")
        with patch.object(self.store, "save_ownership", side_effect=fail_after_first_write):
            with self.assertRaises(CreationError) as error:
                self.provision.add(1000, "Project", name="project")
        self.assertFalse(error.exception.retained)
        self.assertEqual(self.store.ownership(), self.before)
        self.assertNotIn("project", self.backend.accounts.users)
        self.assertFalse(self.store.has_settings("project"))

    def test_failed_activation_is_rolled_back_while_account_is_still_expired(self):
        self.backend.activation_failure = True
        with self.assertRaises(CreationError) as error:
            self.provision.add(1000, "Project", name="project")
        self.assertFalse(error.exception.retained)
        self.assertEqual(self.store.ownership(), self.before)
        self.assertIn(("remove", "project"), self.backend.events)

    def test_activated_or_changed_account_is_retained_instead_of_deleted(self):
        self.backend.activation_failure = self.backend.activation_committed = True
        with self.assertRaises(CreationError) as error:
            self.provision.add(1000, "Project", name="project")
        self.assertTrue(error.exception.retained)
        self.assertIn("project", self.backend.accounts.users)
        self.assertEqual(self.store.ownership().owner("project"), "main")
        self.assertNotIn(("remove", "project"), self.backend.events)

    def test_unproven_partial_useradd_is_not_deleted(self):
        self.backend.partial_create = True
        with self.assertRaises(CreationError) as error:
            self.provision.add(1000, "Project", name="project")
        self.assertTrue(error.exception.retained)
        self.assertIn("project", self.backend.accounts.users)
        self.assertNotIn(("remove", "project"), self.backend.events)
        self.assertEqual(self.store.ownership(), self.before)

    def test_changed_root_metadata_is_retained_without_clobbering_another_operation(self):
        original = self.store.save_ownership
        def changed_by_administrator(mapping):
            original(Ownership.parse(mapping.serialize() + "third fourth\n"))
            raise OSError("fixture unrelated administrator update")
        with patch.object(self.store, "save_ownership", side_effect=changed_by_administrator):
            with self.assertRaises(CreationError) as error:
                self.provision.add(1000, "Project", name="project")
        self.assertTrue(error.exception.retained)
        self.assertEqual(self.store.ownership().owner("fourth"), "third")
        self.assertNotIn(("remove", "project"), self.backend.events)

    def test_changed_fresh_settings_are_not_deleted_by_cleanup(self):
        def change_settings_then_fail(_):
            self.store.save_settings("project", DesktopSettings(password="home"))
            raise OSError("fixture changed profile settings")
        with patch.object(self.store, "save_ownership", side_effect=change_settings_then_fail):
            with self.assertRaises(CreationError) as error:
                self.provision.add(1000, "Project", name="project")
        self.assertTrue(error.exception.retained)
        self.assertEqual(self.store.settings("project").password, "home")
        self.assertNotIn(("remove", "project"), self.backend.events)

    def test_out_of_pool_uid_is_rolled_back_before_metadata_or_activation(self):
        self.backend.next_uid = 17384
        with self.assertRaises(CreationError) as error:
            self.provision.add(1000, "Project", name="project")
        self.assertFalse(error.exception.retained)
        self.assertEqual(self.store.ownership(), self.before)
        self.assertNotIn("project", self.backend.accounts.users)
        self.assertFalse(any(item[0] == "activate" for item in self.backend.events))

    def test_live_uid_is_not_forcibly_removed_during_rollback(self):
        self.backend.processes = True
        with self.assertRaises(CreationError) as error:
            self.provision.add(1000, "Project", name="project")
        self.assertTrue(error.exception.retained)
        self.assertNotIn(("remove", "project"), self.backend.events)


class UnprivilegedProvisionTest(unittest.TestCase):
    @unittest.skipIf(os.geteuid() == 0, "ordinary user role")
    def test_account_creation_is_denied_before_backend_access(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            store = ConfigStore(root / "etc", root / "legacy", root / "run")
            backend = FixtureBackend(store)
            with self.assertRaises(PermissionError):
                AccountProvisioner(store, backend).add(1000, "Project")
            self.assertEqual(backend.events, [])
            self.assertEqual(list(root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
