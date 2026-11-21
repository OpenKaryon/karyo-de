# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import io
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.entrypoints import add, execute, list_desktops, main, read_password, switch_existing
from karyo_profiles.switching import DesktopStopped
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


class DesktopControlEntrypointTest(unittest.TestCase):
    def test_switch_passes_only_valid_target_and_authenticated_caller(self):
        backend = Mock()
        backend.return_value.switch.return_value = {"user": "work", "vt": 8, "changed": True}
        authorize = Mock(return_value=HOME)
        environment = {"PKEXEC_UID": "1000", "USER": "forged"}
        result = switch_existing(["work"], environment=environment, store="store",
                                 authorize=authorize, switcher=backend)
        authorize.assert_called_once_with(environment)
        backend.assert_called_once_with("store")
        backend.return_value.switch.assert_called_once_with(HOME, "work")
        self.assertEqual(result["user"], "work")

    def test_enabled_runtime_uses_fixed_host_packet_after_local_authorization(self):
        authorize, request, backend = Mock(return_value=HOME), Mock(return_value={"user": "work"}), Mock()
        result = switch_existing(["work"], environment={"PKEXEC_UID": "1000"}, store="store",
                                 authorize=authorize, switcher=backend, runtime=lambda: True, request=request)
        self.assertEqual(result, {"user": "work"})
        request.assert_called_once_with({"version": 1, "operation": "switch", "name": "work"})
        backend.assert_not_called()
        request.reset_mock()
        authorize.side_effect = PermissionError("inactive")
        with self.assertRaises(PermissionError):
            switch_existing(["work"], environment={}, store="store", authorize=authorize,
                            runtime=lambda: True, request=request)
        request.assert_not_called()

    def test_switch_has_no_arbitrary_owner_session_command_or_option_abbreviation(self):
        for arguments in ([], ["work", "other"], ["--owner", "other", "work"],
                          ["--session", "c2"], ["--command", "/bin/sh"], ["../work"],
                          ["--help", "work"], ["work\n"]):
            with self.subTest(arguments=arguments):
                backend, authorize = Mock(), Mock(return_value=HOME)
                with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit):
                    switch_existing(arguments, environment={}, store="store", authorize=authorize, switcher=backend)
                backend.assert_not_called()
                authorize.assert_not_called()

    def test_switch_authentication_failure_never_constructs_privileged_controller(self):
        backend = Mock()
        with self.assertRaises(PermissionError):
            switch_existing(["work"], environment={}, store="store",
                            authorize=Mock(side_effect=PermissionError("Inactive")), switcher=backend)
        backend.assert_not_called()

    def test_listing_uses_own_family_under_configuration_lock(self):
        store = Mock()
        store.locked.return_value = contextlib.nullcontext(store)
        authorize = Mock(return_value=HOME)
        accounts, sessions = Mock(), Mock()
        with patch("karyo_profiles.entrypoints.listing", return_value=[{"user": "home"}]) as listing:
            result = list_desktops([], environment={}, store=store, authorize=authorize,
                                   accounts=accounts, sessions=sessions)
        self.assertEqual(result, [{"user": "home"}])
        store.locked.assert_called_once_with()
        listing.assert_called_once_with(HOME, store, accounts.return_value, sessions.return_value)

    def test_listing_rejects_selection_arguments_and_inactive_callers(self):
        accounts, sessions, store = Mock(), Mock(), Mock()
        for args in (["other"], ["--owner", "other"], ["--json"]):
            with self.assertRaises(ValueError):
                list_desktops(args, environment={}, store=store, accounts=accounts, sessions=sessions)
        with self.assertRaises(PermissionError):
            list_desktops([], environment={}, store=store, accounts=accounts, sessions=sessions,
                          authorize=Mock(side_effect=PermissionError("Inactive")))
        accounts.assert_not_called()
        sessions.assert_not_called()
        store.locked.assert_not_called()

    def test_policy_binds_only_fixed_helpers_to_active_local_callers(self):
        source = Path(__file__).resolve().parents[1]
        actions = ET.parse(source / "org.karyo.desktops.policy.in").getroot().findall("action")
        self.assertEqual({value.attrib["id"] for value in actions},
                         {"org.karyo.desktops." + role for role in ("add", "list", "switch")})
        for action in actions:
            role = action.attrib["id"].rsplit(".", 1)[1]
            paths = [item.text for item in action.findall("annotate")
                     if item.attrib.get("key") == "org.freedesktop.policykit.exec.path"]
            self.assertEqual(paths, ["@CMAKE_INSTALL_FULL_LIBEXECDIR@/karyo/desktop-" + role])
            self.assertEqual({item.tag: item.text for item in action.find("defaults")},
                             {"allow_any": "no", "allow_inactive": "no", "allow_active": "yes"})

    def test_stopped_session_has_distinct_exit_without_implicit_start(self):
        with patch("karyo_profiles.entrypoints.execute", side_effect=DesktopStopped("No running session")), contextlib.redirect_stderr(io.StringIO()) as error:
            self.assertEqual(main("switch"), 3)
        self.assertIn("No running session", error.getvalue())


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
