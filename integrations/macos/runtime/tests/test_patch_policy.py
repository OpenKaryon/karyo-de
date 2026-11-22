# SPDX-License-Identifier: GPL-3.0-or-later
"""Restrict real runtime patches before applying any bytes to a private shadow."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from patch_policy import parse_paths
from verify_patches import capture_patches, command

DIFF = b"--- a/src/startup/darling.c\n+++ b/src/startup/darling.c\n@@ -1 +1 @@\n-old\n+new\n"


class PatchPolicy(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.recipe = Path(__file__).resolve().parents[1] / "patches"

    def test_actual_ordered_file_hashes_and_footprint_are_captured(self):
        patches, files, added = capture_patches(self.recipe)
        self.assertEqual(len(patches), 29)
        self.assertIn("src/startup/darling.c", files)
        self.assertIn("src/external/cocotron/CoreText/CTFontDescriptorInternal.h", added)
        self.assertEqual(added, {"src/external/cocotron/CoreText/CTFontDescriptorInternal.h"})

    def test_changed_patch_and_unlisted_patch_are_rejected(self):
        recipe = self.root / "recipe"
        shutil.copytree(self.recipe, recipe)
        first = sorted(recipe.glob("*.patch"))[0]
        first.write_bytes(first.read_bytes() + b"changed\n")
        with self.assertRaisesRegex(ValueError, "checksum changed"):
            capture_patches(recipe)
        first.write_bytes((self.recipe / first.name).read_bytes())
        (recipe / "0022-unlisted.patch").write_bytes(DIFF)
        with self.assertRaisesRegex(ValueError, "Unlisted"):
            capture_patches(recipe)

    def test_series_cannot_reorder_or_select_other_revisions(self):
        recipe = self.root / "recipe"
        shutil.copytree(self.recipe, recipe)
        original = json.loads((recipe / "series.json").read_text())
        for mutation in ("source", "cocotron", "reorder", "count", "escape"):
            record = copy.deepcopy(original)
            if mutation == "source": record["root_revision"] = "0" * 40
            if mutation == "cocotron": record["cocotron_revision"] = "0" * 40
            if mutation == "reorder": record["patches"].reverse()
            if mutation == "count": record["patches"].pop()
            if mutation == "escape": record["patches"][0]["file"] = "../outside.patch"
            (recipe / "series.json").write_text(json.dumps(record))
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                capture_patches(recipe)

    def test_valid_counted_hunks_preserve_header_looking_source_lines(self):
        diff = b"--- a/src/startup/darling.c\n+++ b/src/startup/darling.c\n@@ -1,2 +1,2 @@\n---- source text\n+++ source text\n common\n"
        self.assertEqual(parse_paths(diff), [{"path": "src/startup/darling.c", "added": False, "deleted": False}])

    def test_paths_modes_renames_and_incomplete_hunks_fail(self):
        for data in (DIFF.replace(b"src/startup/darling.c", b"../outside"),
                     DIFF.replace(b"src/startup/darling.c", b"src/other.c"),
                     DIFF.replace(b"src/startup/darling.c", b".git/config"),
                     DIFF.replace(b"@@ -1 +1 @@", b"@@ -1,2 +1 @@"),
                     b"old mode 100644\nnew mode 120000\n" + DIFF,
                     b"rename from src/startup/darling.c\n" + DIFF,
                     b"GIT binary patch\n" + DIFF,
                     DIFF + DIFF):
            with self.subTest(data=data[:80]), self.assertRaises(ValueError):
                parse_paths(data)

    @unittest.skipUnless(shutil.which("git"), "native Git fixture")
    def test_real_forward_reverse_ignores_inherited_git_dir(self):
        path = self.root / "src/startup/darling.c"
        path.parent.mkdir(parents=True)
        path.write_bytes(b"old\n")
        entry = {"file": "fixture.patch", "data": DIFF}
        with patch.dict(os.environ, {"GIT_DIR": str(self.root / "unrelated")}, clear=False):
            command(self.root, entry, check=True)
            command(self.root, entry)
            self.assertEqual(path.read_bytes(), b"new\n")
            command(self.root, entry, reverse=True, check=True)
            command(self.root, entry, reverse=True)
        self.assertEqual(path.read_bytes(), b"old\n")
        self.assertFalse((self.root / "unrelated").exists())


if __name__ == "__main__":
    unittest.main()
