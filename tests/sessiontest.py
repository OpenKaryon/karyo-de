# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the launcher using isolated command fixtures, without starting a desktop."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]


class SessionLauncherTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        data = self.root / "share/karyo/session"
        shutil.copytree(SOURCE / "session/labwc", data / "labwc")
        (data / "labwc/autostart").chmod(0o755)
        for helper in ("keyboard", "renderer"):
            shutil.copyfile(SOURCE / "session" / helper, data / helper)
        source = (SOURCE / "session/karyo-session.in").read_text()
        launcher = source.replace("@CMAKE_INSTALL_FULL_DATADIR@", str(self.root / "share"))
        self.launcher = self.bin / "karyo-session"
        self.launcher.write_text(launcher)
        self.launcher.chmod(0o755)
        for name in ("karyo-shell", "karyo-lock"):
            self.program(name, "#!/bin/sh\nexit 0\n")
        self.program("labwc", """#!/bin/sh
printf '%s\n' "$XDG_CURRENT_DESKTOP" "$XDG_SESSION_TYPE" "$QT_STYLE_OVERRIDE" "$@" > "$PROBE"
exit 7
""")
        self.program("mkdir", '#!/bin/sh\nexec /bin/mkdir "$@"\n')
        self.env = dict(os.environ, PATH=str(self.bin),
                        HOME=str(self.root), XDG_RUNTIME_DIR=str(self.root),
                        XDG_STATE_HOME=str(self.root / "state"), PROBE=str(self.root / "probe"),
                        DBUS_SESSION_BUS_ADDRESS="unix:path=/fixture")
        self.env["XDG_CURRENT_DESKTOP"] = "ExistingDesktop"
        self.env["WLR_RENDERER"] = "pixman"

    def program(self, name, text):
        path = self.bin / name
        path.write_text(text)
        path.chmod(0o755)

    def run_launcher(self, *args):
        return subprocess.run([str(self.launcher), *args], env=self.env,
                              capture_output=True, text=True)

    def test_preflight_and_missing_backend(self):
        self.assertEqual(self.run_launcher("--check").returncode, 0)
        (self.bin / "karyo-shell").unlink()
        result = self.run_launcher("--check")
        self.assertEqual(result.returncode, 1)
        self.assertIn("karyo-shell", result.stderr)

    def test_scoped_environment_and_compositor_status(self):
        result = self.run_launcher()
        self.assertEqual(result.returncode, 7)
        lines = (self.root / "probe").read_text().splitlines()
        self.assertEqual(lines[:4], ["Karyo", "wayland", "Karyo", "-C"])
        self.assertEqual(lines[4], str(self.root / "share/karyo/session/labwc"))
        self.assertEqual(self.env["XDG_CURRENT_DESKTOP"], "ExistingDesktop")

    def test_preflight_requires_configuration_helpers(self):
        (self.root / "share/karyo/session/keyboard").unlink()
        result = self.run_launcher("--check")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("configuration is unavailable", result.stderr)

    def test_user_profile_overrides_system_and_legacy_settings(self):
        self.program("labwc", '#!/bin/sh\nprintf "%s\\n" "$XKB_DEFAULT_LAYOUT" "$WLR_RENDERER" > "$PROBE"\n')
        for directory, layout in (("openkaryon", "us"), ("karyo", "ru,us")):
            profile = self.root / ".config" / directory / "session.env"
            profile.parent.mkdir(parents=True)
            profile.write_text(f"XKB_DEFAULT_LAYOUT={layout}\nWLR_RENDERER=gles2\n")
        result = self.run_launcher()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "probe").read_text().splitlines(), ["ru,us", "gles2"])

    def test_shell_restart_limit(self):
        self.env["SHELL_PROBE"] = str(self.root / "shell-runs")
        self.env["AUTOSTART_PROBE"] = str(self.root / "autostart-runs")
        self.program("dex", '#!/bin/sh\nprintf "%s\\n" "$*" >> "$AUTOSTART_PROBE"\n')
        self.program("karyo-shell", '#!/bin/sh\nprintf "run\\n" >> "$SHELL_PROBE"\nexit 1\n')
        self.program("sleep", "#!/bin/sh\nexit 0\n")
        self.program("date", "#!/bin/sh\necho 100\n")
        self.program("dbus-update-activation-environment", "#!/bin/sh\nexit 0\n")
        autostart = self.root / "share/karyo/session/labwc/autostart"
        result = subprocess.run([str(autostart)], env=self.env,
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0)
        self.assertEqual((self.root / "shell-runs").read_text().splitlines(), ["run"] * 5)
        self.assertEqual((self.root / "probe").read_text().splitlines()[-1], "--exit")
        self.assertEqual((self.root / "autostart-runs").read_text().splitlines(),
                         ["--autostart --environment Karyo"])

    def test_private_bus_fallback_and_argument_validation(self):
        self.program("dbus-run-session", """#!/bin/sh
[ "$1" = -- ] && shift
export DBUS_SESSION_BUS_ADDRESS=unix:path=/fixture
exec "$@"
""")
        self.env.pop("DBUS_SESSION_BUS_ADDRESS")
        self.assertEqual(self.run_launcher().returncode, 7)
        self.assertEqual(self.run_launcher("--unknown").returncode, 2)


if __name__ == "__main__":
    unittest.main()
