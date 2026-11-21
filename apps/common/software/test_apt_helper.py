# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate privileged request boundaries; never execute APT in these tests."""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("apt_helper", Path(__file__).with_name("apt_helper.py"))
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class AptHelperTest(unittest.TestCase):
    def test_supported_requests(self):
        self.assertEqual(helper.arguments(["update"]), ["/usr/bin/apt-get", "update"])
        self.assertEqual(helper.arguments(["upgrade"])[-1], "full-upgrade")
        for action in ("install", "remove"):
            self.assertEqual(helper.arguments([action, "libfoo1:amd64", "python3-pip"])[-4:],
                             [action, "--", "libfoo1:amd64", "python3-pip"])

    def test_options_paths_versions_and_control_characters_rejected(self):
        for request in ([], ["purge", "foo"], ["update", "foo"], ["upgrade", "foo"], ["install"],
                        ["remove"], ["install", "-oAPT::Update::Pre-Invoke::=evil"],
                        ["install", "/tmp/evil.deb"], ["install", "foo=1"], ["install", "foo/stable"],
                        ["install", "foo\n"], ["install", "foo;id"], ["install", "Foo"],
                        ["install", "foo:amd64:extra"], ["install", "foo", "--allow-unauthenticated"],
                        ["install"] + ["foo"] * 257):
            with self.subTest(request=request):
                with self.assertRaises(ValueError):
                    helper.arguments(request)

    def test_unprivileged_and_invalid_requests_never_execute(self):
        with patch.object(helper.os, "execve") as execute, patch.object(helper.os, "geteuid", return_value=1000):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(helper.main(["update"]), 1)
                self.assertEqual(helper.main(["install", "--bad"]), 2)
            execute.assert_not_called()

    def test_fixed_executable_environment_directory_and_input(self):
        class Replaced(Exception):
            pass
        # Mock every process-affecting call, so the test runner is unchanged.
        with patch.object(helper.os, "execve", side_effect=Replaced) as execute, \
             patch.object(helper.os, "geteuid", return_value=0), \
             patch.object(helper.os, "chdir") as directory, \
             patch.object(helper.os, "umask") as mask, \
             patch.object(helper.os, "dup2") as stdin:
            with patch.dict(helper.os.environ, {"APT_CONFIG": "/tmp/evil", "PATH": "/tmp", "PYTHONPATH": "/tmp"}):
                with self.assertRaises(Replaced):
                    helper.main(["install", "python3-pip"])
            directory.assert_called_once_with("/")
            mask.assert_called_once_with(0o022)
            self.assertEqual(stdin.call_args.args[1], 0)
            executable, argv, environment = execute.call_args.args
            self.assertEqual(executable, "/usr/bin/apt-get")
            self.assertEqual(argv[-3:], ["install", "--", "python3-pip"])
            self.assertEqual(environment["HOME"], "/root")
            self.assertEqual(environment["PATH"], "/usr/sbin:/usr/bin:/sbin:/bin")
            self.assertNotIn("APT_CONFIG", environment)
            self.assertNotIn("PYTHONPATH", environment)
            self.assertEqual(environment["DEBIAN_FRONTEND"], "noninteractive")


if __name__ == "__main__":
    unittest.main()
