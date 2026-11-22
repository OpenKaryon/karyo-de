# SPDX-License-Identifier: GPL-3.0-or-later
"""Preserve repeated header inclusion with reproducible compiler path maps."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == "posix" and shutil.which("clang"), "native compiler prefix mapping")
class RubyFileMap(unittest.TestCase):
    def test_actual_self_include_survives_file_prefix_mapping(self):
        patch = Path(__file__).resolve().parents[1] / "patches/0026-ruby-counter-relative-self-include.patch"
        lines = patch.read_text().splitlines()
        before = next(line[1:] for line in lines if line.startswith("-#include"))
        after = next(line[1:] for line in lines if line.startswith("+#include"))
        with tempfile.TemporaryDirectory(prefix="karyo-ruby-map-") as temporary:
            root = Path(temporary)
            headers = root / "headers"
            headers.mkdir()
            source = root / "main.c"
            source.write_text('#include "headers/debug_counter.h"\nint main(void) { return RB_DEBUG_COUNTER_MAX != 1; }\n')
            for include, fixed in ((before, False), (after, True)):
                header = '#ifdef RB_DEBUG_COUNTER\nRB_DEBUG_COUNTER(example)\n#endif\n#ifndef RUBY_DEBUG_COUNTER_H\n#define RUBY_DEBUG_COUNTER_H 1\nenum counter {\n#define RB_DEBUG_COUNTER(name) RB_DEBUG_COUNTER_##name,\n'
                (headers / "debug_counter.h").write_text(header + include + '\nRB_DEBUG_COUNTER_MAX\n#undef RB_DEBUG_COUNTER\n};\n#endif\n')
                result = subprocess.run(["clang", "-ffile-prefix-map=" + str(root) + "=/unavailable-build-path",
                                         str(source), "-o", str(root / "counter")], capture_output=True, text=True)
                if fixed:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([str(root / "counter")], check=True)
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("file not found", result.stderr)
