# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep format-security enabled for the actual patched assertion arguments."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == "posix" and shutil.which("clang"), "native Objective-C compiler")
class FoundationFormat(unittest.TestCase):
    def test_exception_names_are_data_for_literal_format_strings(self):
        patch = Path(__file__).resolve().parents[1] / "patches/0025-foundation-literal-assertion-format.patch"
        lines = patch.read_text().splitlines()
        before = [line[1:].strip() for line in lines if line.startswith("-") and "NSCAssert(" in line]
        after = [line[1:].strip() for line in lines if line.startswith("+") and "NSCAssert(" in line]
        self.assertEqual(len(before), 2)
        self.assertEqual(len(after), 2)
        declarations = r'''
@class NSString;
extern NSString *const NSInternalInconsistencyException;
extern void report(NSString *, ...) __attribute__((format(__NSString__, 1, 2)));
#define Nil ((void *)0)
#define NSCAssert(condition, description, ...) do { if (!(condition)) report(description, ##__VA_ARGS__); } while (0)
void probe(void *concreteClass) {
'''
        with tempfile.TemporaryDirectory(prefix="karyo-foundation-format-") as temporary:
            source = Path(temporary) / "assert.m"
            for old, new in zip(before, after):
                for call, fixed in ((old, False), (new, True)):
                    source.write_text(declarations + call + "\n}\n")
                    result = subprocess.run(["clang", "-x", "objective-c", "-fsyntax-only", "-Wformat",
                                             "-Werror=format-security", str(source)], capture_output=True, text=True)
                    if fixed:
                        self.assertEqual(result.returncode, 0, result.stderr)
                    else:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("format string is not a string literal", result.stderr)
