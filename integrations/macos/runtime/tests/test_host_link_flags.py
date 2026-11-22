# SPDX-License-Identifier: GPL-3.0-or-later
"""Build native ELF targets using the patch's real CMake flag-routing block."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == "posix" and all(shutil.which(t) for t in ("cmake", "ninja", "readelf")), "native ELF/CMake fixture")
class HostLinkFlags(unittest.TestCase):
    def test_native_hardening_survives_while_marked_guest_targets_exclude_elf_flags(self):
        patch = Path(__file__).resolve().parents[1] / "patches/0023-separate-host-elf-link-flags.patch"
        added = [line[1:] for line in patch.read_text().splitlines() if line.startswith("+") and not line.startswith("+++")]
        start = added.index("# Keep host ELF linker policy separate from ld64's Mach-O targets.")
        end = added.index("# End host ELF linker policy boundary.")
        block = "\n".join(added[start:end + 1])
        marker = next(line for line in added if "PROPERTY DARLING_MACHO TRUE" in line)
        with tempfile.TemporaryDirectory(prefix="karyo-link-boundary-") as temporary:
            root = Path(temporary)
            (root / "main.c").write_text("int main(void) { return 0; }\n")
            (root / "lib.c").write_text("int value(void) { return 1; }\n")
            cmake = "cmake_minimum_required(VERSION 3.21)\nproject(LinkBoundary C)\n" + block + "\n"
            for name, declaration in (("executable", "add_executable(NAME main.c)"),
                                      ("shared", "add_library(NAME SHARED lib.c)"),
                                      ("module", "add_library(NAME MODULE lib.c)")):
                for role in ("host", "guest"):
                    target = role + "_" + name
                    cmake += declaration.replace("NAME", target) + "\n"
                    if role == "guest":
                        cmake += "set(target " + target + ")\n" + marker + "\n"
            (root / "CMakeLists.txt").write_text(cmake)
            build = root / "build"
            args = ["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
            for kind in ("EXE", "SHARED", "MODULE"):
                args += ["-DCMAKE_" + kind + "_LINKER_FLAGS=-Wl,-z,relro",
                         "-DCMAKE_" + kind + "_LINKER_FLAGS_RELEASE=-Wl,-z,now"]
            result = subprocess.run(args, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands"], text=True).splitlines()
            for name, output in (("executable", "host_executable"), ("shared", "libhost_shared.so"), ("module", "libhost_module.so")):
                host = next(line for line in commands if " -o " + output + " " in line)
                guest_output = output.replace("host_", "guest_")
                guest = next(line for line in commands if " -o " + guest_output + " " in line)
                self.assertIn("-Wl,-z,relro", host)
                self.assertIn("-Wl,-z,now", host)
                self.assertNotIn("-Wl,-z,relro", guest)
                self.assertNotIn("-Wl,-z,now", guest)
                program = build / output
                self.assertIn("GNU_RELRO", subprocess.check_output(["readelf", "-l", str(program)], text=True))
                self.assertIn("BIND_NOW", subprocess.check_output(["readelf", "-d", str(program)], text=True))
            subprocess.run([str(build / "host_executable")], check=True)
