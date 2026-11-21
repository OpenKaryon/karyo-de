# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared-password checks with real native chpasswd against private prefixed files."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import AccountOperationError, LocalAccounts, PasswordSync, RootCommands
from karyo_profiles.model import DesktopSettings, Ownership
from karyo_profiles.storage import ConfigStore


class PrefixCommands(RootCommands):
    def __init__(self, root):
        self.root = root
        self.calls = 0

    def run(self, operation, *arguments, input=None):
        self.calls += 1
        if operation != "chpasswd":
            raise AssertionError("Fixture may execute only native chpasswd in its private prefix")
        return super().run(operation, "--prefix", str(self.root), *arguments, input=input)


@unittest.skipUnless(os.geteuid() == 0, "native root credential fixture")
class PasswordTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir="/root")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.etc = self.root / "etc"
        self.etc.mkdir()
        users = (("home", 1000), ("work", 1001), ("separate", 1002), ("other", 2000), ("otherwork", 2001))
        (self.etc / "passwd").write_text("".join(
            f"{name}:x:{uid}:{uid}:QA:/home/{name}:/bin/bash\n" for name, uid in users))
        (self.etc / "group").write_text("".join(f"{name}:x:{uid}:\n" for name, uid in users))
        self.shadow = self.etc / "shadow"
        self.shadow.write_text("".join(f"{name}:!fixture-{name}:12345:0:99999:7:::\n" for name, _ in users))
        os.chmod(self.shadow, 0o600)
        self.store = ConfigStore(self.etc / "karyo", self.etc / "legacy", self.root / "run")
        with self.store.locked():
            self.store.save_ownership(Ownership.parse("home work separate\nother otherwork\n"))
            self.store.save_settings("separate", DesktopSettings(password="separate"))
        self.accounts = LocalAccounts(self.etc / "passwd", self.shadow)
        self.commands = PrefixCommands(self.root)
        self.sync = PasswordSync(self.store, self.accounts, self.commands)
        self.real_shadow = hashlib.sha256(Path("/etc/shadow").read_bytes()).digest()
        self.addCleanup(self.assert_real_shadow_unchanged)

    def assert_real_shadow_unchanged(self):
        self.assertEqual(hashlib.sha256(Path("/etc/shadow").read_bytes()).digest(), self.real_shadow)

    def set_hash(self, name, value):
        rows = []
        for line in self.shadow.read_text().splitlines():
            fields = line.split(":")
            if fields[0] == name:
                fields[1] = value
            rows.append(":".join(fields) + "\n")
        self.shadow.write_text("".join(rows))

    def test_display_caption_uses_local_gecos_and_rejects_control_characters(self):
        self.assertEqual(self.accounts.caption("home"), "QA")
        passwd = self.etc / "passwd"
        original = passwd.read_text()
        passwd.write_text(original.replace(":QA:", ":Home,Office:", 1))
        self.assertEqual(self.accounts.caption("home"), "Home")
        passwd.write_text(original.replace(":QA:", "::", 1))
        self.assertEqual(self.accounts.caption("home"), "home")
        passwd.write_text(original.replace(":QA:", ":bad\\tcaption:", 1).replace("\\t", "\t"))
        with self.assertRaises(ValueError):
            self.accounts.caption("home")

    def test_native_batch_updates_shared_children_and_keeps_separate_passwords(self):
        self.assertEqual(self.sync.all(environment={}), 2)
        self.assertEqual(self.accounts.password_hash("work"), "!fixture-home")
        self.assertEqual(self.accounts.password_hash("otherwork"), "!fixture-other")
        self.assertEqual(self.accounts.password_hash("separate"), "!fixture-separate")
        self.assertEqual(self.commands.calls, 1)
        self.assertEqual(self.sync.all(environment={}), 0)
        self.assertEqual(self.commands.calls, 1)

    def test_shared_child_change_updates_owner_without_crossing_families(self):
        self.set_hash("work", "!fixture-new")
        self.assertEqual(self.sync.after_password_change(
            environment={"PAM_TYPE": "password", "PAM_USER": "work"}), 1)
        self.assertEqual(self.accounts.password_hash("home"), "!fixture-new")
        self.assertEqual(self.accounts.password_hash("otherwork"), "!fixture-otherwork")
        self.assertEqual(self.accounts.password_hash("separate"), "!fixture-separate")

    def test_separate_child_change_does_not_modify_owner_or_siblings(self):
        self.set_hash("separate", "!independent")
        self.assertEqual(self.sync.after_password_change(
            environment={"PAM_TYPE": "password", "PAM_USER": "separate"}), 0)
        self.assertEqual(self.accounts.password_hash("home"), "!fixture-home")
        self.assertEqual(self.commands.calls, 0)

    def test_owner_changes_still_sync_when_owner_has_a_separate_marker(self):
        with self.store.locked():
            self.store.save_settings("home", DesktopSettings(password="separate"))
        self.assertEqual(self.sync.after_password_change(
            environment={"PAM_TYPE": "password", "PAM_USER": "home"}), 1)
        self.assertEqual(self.accounts.password_hash("work"), "!fixture-home")

    def test_empty_owner_locks_children_instead_of_enabling_blank_passwords(self):
        self.set_hash("home", "")
        self.sync.all(environment={})
        self.assertEqual(self.accounts.password_hash("home"), "")
        self.assertEqual(self.accounts.password_hash("work"), "!")

    def test_unconfigured_password_change_has_no_family_side_effect(self):
        self.assertEqual(self.sync.after_password_change(
            environment={"PAM_TYPE": "password", "PAM_USER": "outside"}), 0)
        self.assertEqual(self.commands.calls, 0)

    def test_wrong_system_and_pam_context_are_denied_before_commands(self):
        for environment in ({"PKEXEC_UID": "1000"}, {"PKEXEC_UID": ""}):
            with self.assertRaises(PermissionError):
                self.sync.all(environment=environment)
        for environment in ({}, {"PAM_TYPE": "auth", "PAM_USER": "work"},
                            {"PAM_TYPE": "password", "PAM_USER": "../root"}):
            with self.assertRaises((PermissionError, ValueError)):
                self.sync.after_password_change(environment=environment)
        self.assertEqual(self.commands.calls, 0)

    def test_invalid_owner_or_shadow_prevents_all_batches(self):
        with self.store.locked():
            self.store.save_ownership(Ownership.parse("missing work\n"))
        with self.assertRaises(ValueError):
            self.sync.all(environment={})
        self.assertEqual(self.commands.calls, 0)

    def test_duplicate_uid_and_system_accounts_are_not_desktop_targets(self):
        with (self.etc / "passwd").open("a") as stream:
            stream.write("alias:x:1000:1000:QA:/home/alias:/bin/bash\nroot:x:0:0:root:/root:/bin/bash\n")
        with self.assertRaisesRegex(ValueError, "unique local user ID"):
            self.accounts.account("home")
        with self.assertRaises(ValueError):
            self.accounts.account("root")
        self.assertIsNone(self.accounts.account("missing", missing_ok=True))

    def test_caller_uid_and_staging_expiry_are_validated(self):
        self.assertEqual(self.accounts.by_uid(1001).name, "work")
        for uid in (0, True, "1001", 65534):
            with self.assertRaises(ValueError):
                self.accounts.by_uid(uid)
        self.assertIsNone(self.accounts.expiry_day("work"))
        for value, expected in (("1", 1), ("-1", None), ("", None)):
            rows = []
            for line in self.shadow.read_text().splitlines():
                fields = line.split(":")
                if fields[0] == "work":
                    fields[7] = value
                rows.append(":".join(fields) + "\n")
            self.shadow.write_text("".join(rows))
            self.assertEqual(self.accounts.expiry_day("work"), expected)

    def test_duplicate_or_unsafe_shadow_cannot_supply_credentials(self):
        with self.shadow.open("a") as stream:
            stream.write("home:!duplicate:12345:0:99999:7:::\n")
        with self.assertRaises(ValueError):
            self.sync.all(environment={})
        self.assertEqual(self.commands.calls, 0)
        os.chmod(self.shadow, 0o666)
        with self.assertRaises(ValueError):
            self.accounts.password_hash("work")

    def test_native_failed_encrypted_batch_keeps_every_existing_hash(self):
        before = self.shadow.read_bytes()
        with self.assertRaises(AccountOperationError):
            self.commands.run("chpasswd", "--encrypted", input="work:!changed\nmissing:!bad\n")
        self.assertEqual(self.shadow.read_bytes(), before)

    def test_command_failure_redacts_sensitive_input_output_and_environment(self):
        marker = "not-a-real-password-fixture"
        completed = subprocess.CompletedProcess([], 1, stdout=marker, stderr=marker)
        with patch("karyo_profiles.credentials.subprocess.run", return_value=completed) as run:
            with self.assertRaises(AccountOperationError) as captured:
                RootCommands().run("chpasswd", "--encrypted", input=marker)
        self.assertNotIn(marker, str(captured.exception))
        self.assertNotIn(marker, repr(captured.exception))
        args, kwargs = run.call_args
        self.assertEqual(args[0], ["/usr/sbin/chpasswd", "--encrypted"])
        self.assertEqual(kwargs["input"], marker)
        self.assertEqual(kwargs["env"], {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"})
        self.assertTrue(kwargs["close_fds"])
        self.assertEqual(kwargs["timeout"], 15)
        with self.assertRaises(ValueError):
            RootCommands().run("user-supplied-program")

    def test_timeout_message_omits_sensitive_process_input(self):
        marker = "not-a-real-password-fixture"
        with patch("karyo_profiles.credentials.subprocess.run",
                   side_effect=subprocess.TimeoutExpired(["chpasswd"], 15, output=marker)):
            with self.assertRaises(AccountOperationError) as captured:
                RootCommands().run("chpasswd", "--encrypted", input=marker)
        self.assertNotIn(marker, str(captured.exception))


class UnprivilegedCredentialsTest(unittest.TestCase):
    @unittest.skipIf(os.geteuid() == 0, "ordinary user role")
    def test_root_credential_commands_are_denied_without_subprocess(self):
        with patch("karyo_profiles.credentials.subprocess.run") as run:
            with self.assertRaises(PermissionError):
                RootCommands().run("chpasswd", "--encrypted", input="unused")
            with self.assertRaises(PermissionError):
                PasswordSync.system_context({})
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
