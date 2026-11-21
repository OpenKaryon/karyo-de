# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import io
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.entrypoints import add, execute, main, read_password
from karyo_profiles.provision import CreationError

HOME = LocalAccount("home", 1000, 1000, "/home/home", "/bin/bash")
CHILD = LocalAccount("work", 1001, 1001, "/home/work", "/bin/bash")


class EntrypointTest(unittest.TestCase):
    def invoke_add(self, arguments, **options):
        backend = Mock()
        backend.return_value.add.return_value = CHILD
        authorize = Mock(return_value=HOME)
        password_input = Mock(return_value="synthetic fixture password")
        values = dict(environment={"PKEXEC_UID": "1000"}, store=object(), authorize=authorize,
                      provision=backend, password_input=password_input)
        values.update(options)
        result = add(arguments, **values)
        return result, backend, authorize, password_input

    def test_shared_add_has_authenticated_caller_and_no_password_read(self):
        result, backend, authorize, password_input = self.invoke_add(["work", "--label", "Work"])
        self.assertEqual(result, {"user": "work"})
        authorize.assert_called_once_with({"PKEXEC_UID": "1000"})
        password_input.assert_not_called()
        backend.return_value.add.assert_called_once_with(1000, "Work", name="work", protection="none", password=None)

    def test_separate_password_comes_from_stdin_only_after_authorization(self):
        _, backend, _, password_input = self.invoke_add(["--label", "Experiments", "--protection", "separate"])
        password_input.assert_called_once_with()
        backend.return_value.add.assert_called_once_with(1000, "Experiments", name="", protection="separate",
                                                       password="synthetic fixture password")

    def test_home_protection_retains_shared_password(self):
        _, backend, _, password_input = self.invoke_add(["work", "--protection", "home"])
        password_input.assert_not_called()
        self.assertEqual(backend.return_value.add.call_args.kwargs["protection"], "home")
        self.assertIsNone(backend.return_value.add.call_args.kwargs["password"])

    def test_unauthorized_request_never_reads_password_or_creates_account(self):
        password_input, backend = Mock(), Mock()
        with self.assertRaises(PermissionError):
            self.invoke_add(["--label", "Work", "--protection", "separate"],
                authorize=Mock(side_effect=PermissionError("Inactive")), password_input=password_input, provision=backend)
        password_input.assert_not_called()
        backend.assert_not_called()

    def test_no_owner_executable_path_password_argument_or_abbreviated_option(self):
        for arguments in (["--owner", "someone"], ["--command", "/bin/sh"], ["--password", "fixture"],
                          ["--lab", "Work"], ["--protection", "other"], ["first", "second"]):
            with self.subTest(arguments=arguments), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.invoke_add(arguments)

    def test_only_fixed_system_operations_accept_no_arguments(self):
        with patch("karyo_profiles.entrypoints.PasswordSync") as sync:
            self.assertIsNone(execute("sync", [], environment={}, store="fixture"))
            sync.return_value.all.assert_called_once_with(environment={})
            self.assertIsNone(execute("password-changed", [], environment={"PAM_TYPE": "password"}, store="fixture"))
            sync.return_value.after_password_change.assert_called_once_with(environment={"PAM_TYPE": "password"})
            for operation, arguments in (("shell", []), ("sync", ["home"]), ("password-changed", ["other"])):
                with self.assertRaises(ValueError):
                    execute(operation, arguments, environment={}, store="fixture")

    def test_main_has_machine_success_and_no_system_sync_output(self):
        for result, output in (({"user": "work"}, '{"user": "work"}\n'), (None, "")):
            with patch("karyo_profiles.entrypoints.execute", return_value=result), contextlib.redirect_stdout(io.StringIO()) as stdout:
                self.assertEqual(main("add"), 0)
            self.assertEqual(stdout.getvalue(), output)

    def test_retained_account_exit_is_distinct_and_unexpected_diagnostics_are_redacted(self):
        for error, code, allowed in ((CreationError("work", retained=True), 2, "retained"),
                                    (CreationError("work"), 1, "rolled back"),
                                    (OSError("private native diagnostic"), 1, "administrator review"),
                                    (UnicodeDecodeError("utf-8", b"secret\xff", 6, 7, "bad"), 1, "valid UTF-8")):
            with self.subTest(error=type(error)), patch("karyo_profiles.entrypoints.execute", side_effect=error), contextlib.redirect_stderr(io.StringIO()) as stderr:
                self.assertEqual(main("add"), code)
            self.assertIn(allowed, stderr.getvalue())
            self.assertNotIn("secret", stderr.getvalue())
            self.assertNotIn("private native", stderr.getvalue())


class PasswordPipeTest(unittest.TestCase):
    def read(self, data):
        reader, writer = os.pipe()
        try:
            os.write(writer, data)
            os.close(writer)
            writer = None
            return read_password(reader, timeout=0.2)
        finally:
            os.close(reader)
            if writer is not None:
                os.close(writer)

    def test_utf8_and_boundary_password(self):
        self.assertEqual(self.read("fixture пароль\n".encode()), "fixture пароль")
        self.assertEqual(self.read(b"x" * 1024 + b"\n"), "x" * 1024)

    def test_no_truncated_partial_multiline_or_empty_password(self):
        for data in (b"", b"\n", b"partial", b"a\nb\n", b"bad\r\n", b"bad\0\n", b"x" * 1025 + b"\n", b"\xff\n"):
            with self.subTest(data=data[:20]), self.assertRaises(ValueError):
                self.read(data)

    def test_unclosed_pipe_cannot_keep_root_helper_waiting_indefinitely(self):
        reader, writer = os.pipe()
        try:
            os.write(writer, b"fixture\n")
            with self.assertRaises(TimeoutError):
                read_password(reader, timeout=0.05)
        finally:
            os.close(reader)
            os.close(writer)


class InstalledLauncherTest(unittest.TestCase):
    def test_real_isolated_launcher_ignores_cwd_and_pythonpath_and_writes_no_bytecode(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            libexec = root / "libexec"
            modules = libexec / "karyo/profiles/karyo_profiles"
            modules.mkdir(parents=True)
            (modules / "__init__.py").write_text("")
            (modules / "entrypoints.py").write_text("import sys\n"
                "def main(operation):\n"
                "    assert operation == 'add' and sys.flags.isolated and sys.dont_write_bytecode\n"
                "    print('isolated fixed operation')\n"
                "    return 0\n")
            untrusted = root / "caller"
            untrusted.mkdir()
            marker = untrusted / "unexpected-import"
            injected = "from pathlib import Path; Path(" + repr(str(marker)) + ").touch()\n"
            (untrusted / "sitecustomize.py").write_text(injected)
            (untrusted / "karyo_profiles").mkdir()
            (untrusted / "karyo_profiles/__init__.py").write_text(injected)
            template = (Path(__file__).resolve().parents[1] / "entrypoint.in").read_text()
            launcher = root / "desktop-add"
            launcher.write_text(template.replace("@CMAKE_INSTALL_FULL_LIBEXECDIR@", str(libexec))
                                       .replace("@KARYO_PROFILE_OPERATION@", "add"))
            launcher.chmod(0o755)
            result = subprocess.run([str(launcher)], cwd=untrusted,
                env=dict(os.environ, PYTHONPATH=str(untrusted), PYTHONHOME=str(untrusted)),
                capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "isolated fixed operation")
            self.assertFalse(marker.exists())
            self.assertFalse(list(modules.rglob("*.pyc")))


if __name__ == "__main__":
    unittest.main()
