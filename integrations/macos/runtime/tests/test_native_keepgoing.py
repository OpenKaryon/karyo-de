# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the real packaging build command: gather errors without accepting failure."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'posix' and all(shutil.which(tool) for tool in ('dh_auto_build','cmake','ninja','cc')), 'native debhelper/Ninja')
class NativeKeepGoing(unittest.TestCase):
    def test_independent_target_builds_but_package_command_still_fails(self):
        rules = Path(__file__).resolve().parents[1] / 'packaging/rules'
        command = shlex.split(next(line.strip() for line in rules.read_text().splitlines() if line.lstrip().startswith('dh_auto_build ')))
        with tempfile.TemporaryDirectory(prefix='karyo-keepgoing-') as temporary:
            root=Path(temporary)
            (root/'debian').mkdir()
            (root/'debian/control').write_text('Source: probe\nSection: misc\nPriority: optional\nMaintainer: QA <qa@example.invalid>\nBuild-Depends: debhelper-compat (= 13)\n\nPackage: probe\nArchitecture: any\nDescription: Native diagnostic control\n')
            (root/'debian/changelog').write_text('probe (1.0-1) unstable; urgency=low\n\n  * Native diagnostic control.\n\n -- QA <qa@example.invalid>  Sun, 22 Nov 2026 00:00:00 +0000\n')
            (root/'bad.c').write_text('#error probe-bad-target\n')
            (root/'good.c').write_text('int main(void) { return 0; }\n')
            (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(KeepGoing C)\nadd_executable(bad bad.c)\nadd_executable(good good.c)\nadd_custom_command(TARGET good POST_BUILD COMMAND "${CMAKE_COMMAND}" -E echo probe-good-target-linked)\n')
            configured=subprocess.run(['cmake','-S',str(root),'-B',str(root/'obj-karyo'),'-G','Ninja'],capture_output=True,text=True)
            self.assertEqual(configured.returncode,0,configured.stdout+configured.stderr)
            result=subprocess.run(command,cwd=root,env={**{key:value for key,value in os.environ.items() if key != 'DH_COMPAT'},'DEB_BUILD_OPTIONS':'parallel=1'},capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('probe-bad-target',result.stdout+result.stderr)
            trace = result.stdout + result.stderr
            self.assertIn('probe-good-target-linked', trace)
            self.assertLess(trace.index('probe-bad-target'), trace.rindex('probe-good-target-linked'))
            self.assertFalse((root/'obj-karyo/bad').exists())
            self.assertTrue((root/'obj-karyo/good').exists(),result.stdout+result.stderr)
            subprocess.run([str(root/'obj-karyo/good')],check=True)
