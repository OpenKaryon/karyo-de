# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real definition against objc4's NDEBUG/debug-ness invariant."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == "posix" and all(shutil.which(t) for t in ("cmake", "ninja")), "native compiler fixture")
class ObjcBuildMode(unittest.TestCase):
    def test_standard_configs_and_unspecified_mode_match_runtime_header(self):
        patch = Path(__file__).resolve().parents[1] / "patches/0024-match-objc-runtime-build-mode.patch"
        definition = next(line[1:] for line in patch.read_text().splitlines() if line.startswith("+add_compile_definitions(OBJC_IS_DEBUG_BUILD="))
        with tempfile.TemporaryDirectory(prefix="karyo-objc-mode-") as temporary:
            root = Path(temporary)
            (root / "main.c").write_text('#include <stdio.h>\n#if !NDEBUG\n#define DEBUG 1\n#else\n#define DEBUG 0\n#endif\n#if DEBUG != OBJC_IS_DEBUG_BUILD\n#error mismatch in debug-ness macros\n#endif\nint main(void) { printf("%d\\n", OBJC_IS_DEBUG_BUILD); return 0; }\n')
            (root / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.21)\nproject(ObjcMode C)\n" + definition + "\nadd_executable(mode main.c)\n")
            for mode, expected in (("", 1), ("Debug", 1), ("Release", 0), ("RelWithDebInfo", 0), ("MinSizeRel", 0)):
                build = root / (mode or "default")
                result = subprocess.run(["cmake", "-S", str(root), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + mode], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run(["cmake", "--build", str(build)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(subprocess.check_output([str(build / "mode")], text=True).strip(), str(expected))
