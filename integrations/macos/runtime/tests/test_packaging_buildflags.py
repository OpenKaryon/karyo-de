# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual package's make exports through native dpkg and Clang."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class PackagingBuildFlagsTest(unittest.TestCase):
    def test_mixed_format_package_avoids_gcc_lto_without_losing_native_hardening(self):
        if any(shutil.which(tool) is None for tool in ("make", "dpkg-buildflags", "clang", "readelf")):
            self.skipTest("Native Debian or Ubuntu source SDK required")
        rules = Path(__file__).resolve().parents[1] / "packaging/rules"
        environment = dict(os.environ, DEB_BUILD_MAINT_OPTIONS="", DEB_BUILD_OPTIONS="")
        with tempfile.TemporaryDirectory(prefix="karyo-mixed-flags-") as directory:
            root = Path(directory)
            makefile = root / "Makefile"
            makefile.write_text("include " + str(rules) + "\nflags:\n\tdpkg-buildflags --get CFLAGS\n")
            selected = subprocess.run(["make", "-s", "-f", str(makefile), "flags"], cwd=root,
                                      env=environment, text=True, capture_output=True, check=True)
            flags = shlex.split(selected.stdout)
            self.assertNotIn("-ffat-lto-objects", flags)
            self.assertFalse(any(flag.startswith("-flto") for flag in flags))
            for flag in ("-fstack-protector-strong", "-Wformat", "-Werror=format-security"):
                self.assertIn(flag, flags)
            source = root / "probe.c"
            source.write_text("int main(void) { return 0; }\n")
            guest = root / "guest.o"
            subprocess.run(["clang", "-target", "x86_64-apple-darwin20", *flags, "-c", str(source),
                            "-o", str(guest)], check=True, capture_output=True)
            self.assertEqual(guest.read_bytes()[:4], bytes.fromhex("cffaedfe"))
            native = root / "native"
            subprocess.run(["clang", *flags, str(source), "-o", str(native)], check=True, capture_output=True)
            subprocess.run([str(native)], check=True)
            self.assertEqual(native.read_bytes()[:4], bytes.fromhex("7f454c46"))
            # A real compiler negative control for Ubuntu's default flags.
            baseline = subprocess.check_output(["dpkg-buildflags", "--get", "CFLAGS"], cwd=root,
                                               env=environment, text=True, stderr=subprocess.DEVNULL)
            if "-ffat-lto-objects" in shlex.split(baseline):
                rejected = subprocess.run(["clang", "-target", "x86_64-apple-darwin20", *shlex.split(baseline),
                    "-c", str(source), "-o", str(root / "rejected.o")], text=True, capture_output=True)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn("unsupported option '-ffat-lto-objects'", rejected.stderr)


if __name__ == "__main__":
    unittest.main()
