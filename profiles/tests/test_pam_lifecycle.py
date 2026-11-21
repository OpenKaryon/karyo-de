# SPDX-License-Identifier: GPL-3.0-or-later
"""Run actual maintainer shell code with isolated pam-auth-update/config fixtures."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = "/usr/libexec/karyo/desktop-password-changed"
SOURCE = Path(__file__).resolve().parents[1]


class PamLifecycleTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.common = self.root / "common-password"
        self.common.write_bytes(b"password required pam_unix.so\n")
        self.log = self.root / "calls.jsonl"
        self.stubborn = self.root / "stubborn"
        self.fail = self.root / "fail"
        self.helper = self.root / "helper"
        self.helper.write_bytes(b"#!/bin/sh\nexit 0\n")
        self.helper.chmod(0o755)
        self.tool = self.root / "pam-auth-update"
        self.tool.write_text("#!/usr/bin/python3 -I\nimport json,sys\nfrom pathlib import Path\n"
            + "log=Path(" + repr(str(self.log)) + ")\n"
            + "with log.open('a') as stream: stream.write(json.dumps(sys.argv[1:])+'\\n')\n"
            + "if Path(" + repr(str(self.fail)) + ").exists(): raise SystemExit(23)\n"
            + "if '--remove' in sys.argv and not Path(" + repr(str(self.stubborn)) + ").exists():\n"
            + "    Path(" + repr(str(self.common)) + ").write_bytes(b'password required pam_unix.so\\n')\n")
        self.tool.chmod(0o755)

    def run_script(self, role, action, *extra):
        script = (SOURCE / "package" / role).read_text()
        self.assertNotIn("--force", "\n".join(line for line in script.splitlines() if not line.lstrip().startswith("#")))
        script = script.replace("/usr/sbin/pam-auth-update", str(self.tool))
        script = script.replace("/etc/pam.d/common-password", str(self.common))
        if role == "postinst":
            script = script.replace(HELPER, str(self.helper))
        path = self.root / role
        path.write_text(script)
        path.chmod(0o755)
        return subprocess.run([str(path), action, *extra], capture_output=True, text=True, timeout=5)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_configure_registers_only_through_package_mode(self):
        result = self.run_script("postinst", "configure")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [["--package"]])

    def test_configure_refuses_missing_dependency_and_propagates_tool_failure(self):
        self.helper.unlink()
        self.assertNotEqual(self.run_script("postinst", "configure").returncode, 0)
        self.assertEqual(self.calls(), [])
        self.helper.write_bytes(b"#!/bin/sh\n")
        self.helper.chmod(0o755)
        self.fail.touch()
        self.assertEqual(self.run_script("postinst", "configure").returncode, 23)

    def test_remove_and_deconfigure_unregister_before_accepting_removal(self):
        for action in ("remove", "deconfigure"):
            self.common.write_text("password required pam_exec.so seteuid " + HELPER + "\n")
            result = self.run_script("prerm", action)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn(HELPER, self.common.read_text())
        self.assertEqual(self.calls(), [["--package", "--remove", "karyo-desktops-sync"]] * 2)

    def test_locally_modified_active_reference_blocks_removal_without_overwrite(self):
        data = ("# Admin-owned configuration\npassword required pam_exec.so seteuid " + HELPER + "\n").encode()
        self.common.write_bytes(data)
        self.stubborn.touch()
        result = self.run_script("prerm", "remove")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("package retained", result.stderr)
        self.assertEqual(self.common.read_bytes(), data)

    def test_user_disabled_hook_and_commented_reference_do_not_block_removal(self):
        self.stubborn.touch()
        self.common.write_text("# disabled " + HELPER + "\npassword required pam_unix.so\n")
        result = self.run_script("prerm", "remove")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_tool_failure_prevents_removal(self):
        self.fail.touch()
        self.assertEqual(self.run_script("prerm", "remove").returncode, 23)

    def test_unreadable_oversized_or_malformed_common_stack_does_not_pass(self):
        self.stubborn.touch()
        for data in (b"x" * 131073, b"password\0", b"\xff"):
            with self.subTest(data=data[:12]):
                self.common.write_bytes(data)
                self.assertNotEqual(self.run_script("prerm", "remove").returncode, 0)
        self.common.unlink()
        self.assertNotEqual(self.run_script("prerm", "remove").returncode, 0)
        os.mkfifo(self.common)
        self.assertNotEqual(self.run_script("prerm", "remove").returncode, 0)

    def test_remove_purge_and_aborted_install_cleanup_are_idempotent(self):
        for action in ("remove", "purge", "abort-install", "purge"):
            result = self.run_script("postrm", action)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [["--package", "--remove", "karyo-desktops-sync"]] * 4)

    def test_upgrade_paths_leave_enabled_hook_until_new_version_configures(self):
        for role, action in (("prerm", "upgrade"), ("prerm", "failed-upgrade"),
                             ("postrm", "upgrade"), ("postrm", "failed-upgrade"),
                             ("postinst", "abort-remove")):
            result = self.run_script(role, action, "0.1.0-1")
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
