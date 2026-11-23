# SPDX-License-Identifier: GPL-3.0-or-later
"""Package real Clang DWARF5 through debhelper without losing debug data."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'posix' and all(shutil.which(t) for t in ('clang++','dwz','dh','readelf','dpkg-buildpackage')), 'native debug packaging tools')
class NativeDebugPackaging(unittest.TestCase):
    def test_native_runtime_and_split_dwarf5_debug_data_survive_optional_compaction_skip(self):
        rules = (Path(__file__).resolve().parents[1]/'packaging/rules').read_text()
        block = rules.split('override_dh_dwz:\n',1)[1].split('\n\n',1)[0]
        with tempfile.TemporaryDirectory(prefix='karyo-debug-package-') as temporary:
            root=Path(temporary);source=root/'source';source.mkdir();(source/'debian').mkdir()
            (source/'main.cpp').write_text('int main(int count,char**) { volatile int value=count; return value==42; }\n')
            result=subprocess.run(['clang++','-O2','-g','-gdwarf-5',str(source/'main.cpp'),'-o',str(root/'before')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('.debug_addr',subprocess.check_output(['readelf','-SW',str(root/'before')],text=True))
            original=subprocess.run(['dwz',str(root/'before')],capture_output=True,text=True)
            if original.returncode:
                self.assertIn('debug_addr',original.stderr)
            # Use the real override body, retaining the normal strip/shlibdeps/
            # builddeb chain; this is a complete tiny package, not a mocked tool.
            (source/'Makefile').write_text('all: probe\nprobe: main.cpp\n\tclang++ -O2 -g -gdwarf-5 -Wl,--build-id main.cpp -o probe\ninstall: probe\n\tinstall -D -m755 probe $(DESTDIR)/usr/bin/karyo-debug-probe\nclean:\n\trm -f probe\n')
            (source/'debian/control').write_text('Source: karyo-debug-probe\nSection: misc\nPriority: optional\nMaintainer: QA <qa@example.invalid>\nBuild-Depends: debhelper-compat (= 13), clang\nRules-Requires-Root: no\n\nPackage: karyo-debug-probe\nArchitecture: any\nDepends: ${shlibs:Depends}, ${misc:Depends}\nDescription: Native debug preservation control\n')
            (source/'debian/changelog').write_text('karyo-debug-probe (1.0-1) unstable; urgency=low\n\n  * Native debug preservation control.\n\n -- QA <qa@example.invalid>  Sun, 22 Nov 2026 00:00:00 +0000\n')
            rule=source/'debian/rules';rule.write_text('#!/usr/bin/make -f\n%:\n\tdh $@\n\noverride_dh_dwz:\n'+block+'\n');rule.chmod(0o755)
            result=subprocess.run(['dpkg-buildpackage','-b','-us','-uc','-d'],cwd=source,env={**os.environ,'DEB_BUILD_OPTIONS':'parallel=1'},capture_output=True,text=True,timeout=90)
            self.assertEqual(result.returncode,0,(result.stdout+result.stderr)[-7000:])
            binaries=list(root.glob('karyo-debug-probe_*.deb'));debug=[*root.glob('karyo-debug-probe-dbgsym_*.deb'),*root.glob('karyo-debug-probe-dbgsym_*.ddeb')]
            self.assertEqual(len(binaries),1);self.assertEqual(len(debug),1)
            stage=root/'runtime';symbols=root/'symbols'
            subprocess.run(['dpkg-deb','-x',str(binaries[0]),str(stage)],check=True,capture_output=True)
            subprocess.run(['dpkg-deb','-x',str(debug[0]),str(symbols)],check=True,capture_output=True)
            program=stage/'usr/bin/karyo-debug-probe'
            subprocess.run([str(program)],check=True)
            sections=subprocess.check_output(['readelf','-SW',str(program)],text=True)
            self.assertNotIn('.debug_info',sections);self.assertIn('.gnu_debuglink',sections)
            files=list(symbols.rglob('*.debug'));self.assertTrue(files)
            self.assertTrue(any('.debug_addr' in subprocess.check_output(['readelf','-SW',str(path)],text=True) for path in files))
