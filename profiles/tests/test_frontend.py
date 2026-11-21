# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.frontend import command, main

HELPERS = "/usr/libexec/karyo"


class FrontendTest(unittest.TestCase):
    def test_listing_does_not_forward_owner_or_display_options_to_root(self):
        expected = ["/usr/bin/pkexec", HELPERS + "/desktop-list"]
        self.assertEqual(command(["list"], HELPERS), expected)
        self.assertEqual(command(["list", "--json"], HELPERS), expected)

    def test_switch_preserves_target_literal_and_leaves_privileged_validation_to_helper(self):
        self.assertEqual(command(["switch", "work"], HELPERS),
                         ["/usr/bin/pkexec", HELPERS + "/desktop-switch", "work"])

    def test_generated_child_name_and_label_that_looks_like_an_option_remain_data(self):
        self.assertEqual(command(["add", "--label=--Work"], HELPERS),
            ["/usr/bin/pkexec", HELPERS + "/desktop-add", "--label=--Work", "--protection=none"])

    def test_password_is_never_in_command_and_separate_mode_explicitly_requires_stdin(self):
        self.assertEqual(command(["add", "lab", "--label", "Experiments", "--protection", "separate",
                                  "--password-stdin"], HELPERS),
            ["/usr/bin/pkexec", HELPERS + "/desktop-add", "lab", "--label=Experiments", "--protection=separate"])
        self.assertEqual(command(["add", "work", "--protection", "home"], HELPERS)[-1], "--protection=home")
        for args in (["add", "--protection", "separate"], ["add", "--password-stdin"],
                     ["add", "--protection", "home", "--password-stdin"]):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                command(args, HELPERS)

    def test_no_arbitrary_owner_path_executable_password_or_abbreviated_options(self):
        for args in ([], ["list", "--owner", "other"], ["switch", "../other"], ["switch", "work", "other"],
                     ["add", "--command", "/bin/sh"], ["add", "--password", "secret"],
                     ["add", "--prot", "home"], ["sync"], ["password-changed"], ["start", "work"]):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                command(args, HELPERS)

    def test_exec_replaces_frontend_and_preserves_input_and_result_without_intermediate_shell(self):
        with patch.object(sys, "argv", ["karyo-desktops", "switch", "work"]), patch("os.execv") as execute:
            main(HELPERS)
        execute.assert_called_once_with("/usr/bin/pkexec", ["/usr/bin/pkexec", HELPERS + "/desktop-switch", "work"])

    def test_exec_failure_does_not_disclose_environment_or_native_diagnostics(self):
        with patch.object(sys, "argv", ["karyo-desktops", "list"]), patch("os.execv", side_effect=OSError("private detail")), contextlib.redirect_stderr(io.StringIO()) as error:
            self.assertEqual(main(HELPERS), 1)
        self.assertNotIn("private detail", error.getvalue())

    def test_helper_directory_must_be_absolute_and_cannot_traverse(self):
        for directory in ("relative", "/usr/libexec/../other"):
            with self.subTest(directory=directory), self.assertRaises(ValueError):
                command(["list"], directory)

    def test_real_launcher_ignores_caller_python_injection_and_disables_bytecode(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            libexec = root / "libexec"
            modules = libexec / "karyo/profiles/karyo_profiles"
            modules.mkdir(parents=True)
            (modules / "__init__.py").write_text("")
            (modules / "frontend.py").write_text("import sys\ndef main(directory):\n"
                "    assert sys.flags.isolated and sys.dont_write_bytecode\n"
                "    assert directory.endswith('/libexec/karyo')\n"
                "    print('isolated public frontend')\n    return 3\n")
            untrusted = root / "caller"
            untrusted.mkdir()
            marker = untrusted / "injected"
            injected = "from pathlib import Path; Path(" + repr(str(marker)) + ").touch()\n"
            (untrusted / "sitecustomize.py").write_text(injected)
            (untrusted / "karyo_profiles").mkdir()
            (untrusted / "karyo_profiles/__init__.py").write_text(injected)
            template = (Path(__file__).resolve().parents[1] / "frontend.in").read_text()
            launcher = root / "karyo-desktops"
            launcher.write_text(template.replace("@CMAKE_INSTALL_FULL_LIBEXECDIR@", str(libexec)))
            launcher.chmod(0o755)
            result = subprocess.run([str(launcher), "list"], cwd=untrusted,
                env=dict(os.environ, PYTHONPATH=str(untrusted), PYTHONHOME=str(untrusted)),
                capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 3, result.stderr)
            self.assertEqual(result.stdout.strip(), "isolated public frontend")
            self.assertFalse(marker.exists())
            self.assertFalse(list(modules.rglob("*.pyc")))


if __name__ == "__main__":
    unittest.main()
