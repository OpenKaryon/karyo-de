# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real target option with native behavior and Mach-O LLVM IR."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'posix' and all(shutil.which(t) for t in ('clang++','cmake','ninja')), 'native Clang/CMake tools')
class BootstrapSwitchTables(unittest.TestCase):
    def test_static_bootstrap_option_avoids_relative_tables_without_changing_results(self):
        patch = Path(__file__).resolve().parents[1] / 'patches/0029-bootstrap-unwind-without-switch-tables.patch'
        option = next(line[1:] for line in patch.read_text().splitlines() if line.startswith('+set_target_properties('))
        values = ['alpha','bravo','charlie','delta','echo','foxtrot','golf','hotel',
                  'india','juliet','kilo','lima','mike','november','oscar','papa']
        source = 'extern "C" const char *name(int n) { switch(n) {\n' + ''.join(
            'case '+str(i)+': return "'+value+'";\n' for i,value in enumerate(values)) + 'default: return "unknown"; }}\n'
        main = '#include <cstring>\nextern "C" const char *name(int);\nint main() {\n'
        main += ''.join('if (std::strcmp(name('+str(i)+'),"'+value+'")) return 1;\n' for i,value in enumerate(values))
        main += 'return std::strcmp(name(-1),"unknown") || std::strcmp(name(999),"unknown"); }\n'
        with tempfile.TemporaryDirectory(prefix='karyo-unwind-switch-') as temporary:
            root = Path(temporary)
            (root/'switch.cpp').write_text(source)
            (root/'main.cpp').write_text(main)
            (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(Switch CXX)\n'
                'add_library(default STATIC switch.cpp)\nadd_library(unwind_static STATIC switch.cpp)\n' + option + '\n')
            build = root/'build'
            subprocess.run(['cmake','-S',str(root),'-B',str(build),'-G','Ninja',
                '-DCMAKE_CXX_COMPILER=clang++','-DCMAKE_CXX_FLAGS=-O3 -fPIC -Wformat -Werror=format-security',
                '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'],check=True,capture_output=True,text=True)
            subprocess.run(['cmake','--build',str(build)],check=True,capture_output=True,text=True)
            commands = json.loads((build/'compile_commands.json').read_text())
            for target in ('default','unwind_static'):
                command = next(c['command'] for c in commands if 'CMakeFiles/'+target+'.dir/' in c['command'])
                flags = shlex.split(command)
                self.assertEqual('-fno-jump-tables' in flags,target == 'unwind_static')
                self.assertIn('-O3',flags)
                self.assertIn('-Werror=format-security',flags)
                program = root/(target+'-native')
                subprocess.run(['clang++',str(root/'main.cpp'),str(build/('lib'+target+'.a')),'-o',str(program)],check=True,capture_output=True,text=True)
                subprocess.run([str(program)],check=True,capture_output=True,text=True)
                ir = root/(target+'.ll')
                args = ['clang++','-target','x86_64-apple-darwin20','-O3','-fPIC','-S','-emit-llvm']
                if '-fno-jump-tables' in flags:
                    args.append('-fno-jump-tables')
                subprocess.run(args+[str(root/'switch.cpp'),'-o',str(ir)],check=True,capture_output=True,text=True)
                text = ir.read_text()
                self.assertEqual('switch.table' in text,target == 'default')
                self.assertEqual('"no-jump-tables"="true"' in text,target == 'unwind_static')
