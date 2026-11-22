# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual screen forwarding call with format-security retained."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'posix' and shutil.which('clang'), 'native C format compiler')
class ScreenFormat(unittest.TestCase):
    def test_existing_message_stays_literal_data(self):
        patch = Path(__file__).resolve().parents[1] / 'patches/0027-screen-literal-message-format.patch'
        lines = patch.read_text().splitlines()
        before = next(line[1:] for line in lines if line.startswith('-') and 'LMsg(' in line)
        after = next(line[1:] for line in lines if line.startswith('+') and 'LMsg(' in line)
        header = '#include <stdarg.h>\n#include <stdio.h>\n#include <string.h>\nstatic char output[128];\nstatic int code;\nvoid LMsg(int err, const char *format, ...) __attribute__((format(printf,2,3)));\nvoid LMsg(int err, const char *format, ...) { va_list args; code=err; va_start(args,format); vsnprintf(output,sizeof output,format,args); va_end(args); }\nvoid WMsg(int err, char *str) {\n'
        footer = '\n}\nint main(void) { char value[]="100% %n literal"; WMsg(9,value); return code!=9 || strcmp(output,value)!=0; }\n'
        with tempfile.TemporaryDirectory(prefix='karyo-screen-format-') as temporary:
            root = Path(temporary)
            source = root / 'screen.c'
            for call, fixed in ((before, False), (after, True)):
                source.write_text(header + call + footer)
                result = subprocess.run(['clang','-Wformat','-Werror=format-security',str(source),'-o',str(root/'screen')],capture_output=True,text=True)
                if fixed:
                    self.assertEqual(result.returncode,0,result.stderr)
                    subprocess.run([str(root/'screen')],check=True)
                else:
                    self.assertNotEqual(result.returncode,0)
                    self.assertIn('format string is not a string literal',result.stderr)
