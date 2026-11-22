# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual version block from the reviewed upstream patch with CMake."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_swift import ROOT_REVISION


@unittest.skipUnless(shutil.which("cmake"), "native CMake fixture")
class SourceVersion(unittest.TestCase):
    def test_pinned_archive_identity_and_malformed_revisions(self):
        patch = Path(__file__).resolve().parents[1] / "patches/0022-explicit-source-archive-version.patch"
        added = [line[1:] for line in patch.read_text().splitlines() if line.startswith("+") and not line.startswith("+++")]
        # The archive branch is the literal code introduced by this patch.
        block = "\n".join(added)
        block = block[:block.index("\nelse()")]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            script = root / "check.cmake"
            script.write_text(block + '\nendif()\nfile(WRITE "${OUTPUT}" "${GIT_BRANCH}\\n${GIT_COMMIT_HASH}\\n")\n')
            output = root / "version"
            result = subprocess.run(["cmake", "-DDARLING_SOURCE_REVISION=" + ROOT_REVISION,
                                     "-DOUTPUT=" + str(output), "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_text(), "source-archive\n" + ROOT_REVISION + "\n")
            for value in ("", "main", "abc", "A" * 40, "0" * 39 + ";"):
                output.unlink(missing_ok=True)
                result = subprocess.run(["cmake", "-DDARLING_SOURCE_REVISION=" + value,
                                         "-DOUTPUT=" + str(output), "-P", str(script)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, value)
                self.assertFalse(output.exists())
