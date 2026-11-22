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

    def test_every_remaining_diagnostic_keeps_percent_characters_as_data(self):
        patch = Path(__file__).resolve().parents[1] / 'patches/0028-screen-literal-diagnostics.patch'
        lines = patch.read_text().splitlines()
        before = [line[1:].strip() for line in lines if line.startswith('-') and not line.startswith('---')]
        after = [line[1:].strip() for line in lines if line.startswith('+') and not line.startswith('+++')]
        self.assertEqual(len(before), 30)
        self.assertEqual(len(after), 30)
        header = r'''#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static char output[128];
static int code;
static void record(int err, const char *format, va_list args) {
    code=err; vsnprintf(output,sizeof output,format,args);
}
#define MESSAGE(name) \
void name(int err,const char *format,...) __attribute__((format(printf,2,3))); \
void name(int err,const char *format,...) { va_list args; va_start(args,format); record(err,format,args); va_end(args); }
MESSAGE(Msg)
MESSAGE(Panic)
int main(void) {
    char value[]="100% %n literal";
    char *strnomem=value, *LockEnd=value, *command=value;
    char **cmdv=&command;
    int errno=9;
'''
        with tempfile.TemporaryDirectory(prefix='karyo-screen-diagnostics-') as temporary:
            root = Path(temporary)
            source, program = root/'screen.c', root/'screen'
            for number, pair in enumerate(zip(before,after)):
                for fixed, call in enumerate(pair):
                    footer = '\nreturn 0; }\n' if 'printf(' in call else '\nreturn strcmp(output,value)!=0 || code!=' + ('9' if 'errno' in call else '0') + '; }\n'
                    source.write_text(header + call + footer)
                    result = subprocess.run(['clang','-Wformat','-Werror=format-security',str(source),'-o',str(program)],capture_output=True,text=True)
                    with self.subTest(call=number, fixed=bool(fixed)):
                        if fixed:
                            self.assertEqual(result.returncode,0,result.stderr)
                            run = subprocess.run([str(program)],check=True,capture_output=True,text=True)
                            self.assertEqual(run.stdout,'100% %n literal' if 'printf(' in call else '')
                        else:
                            self.assertNotEqual(result.returncode,0)
                            self.assertIn('format string is not a string literal',result.stderr)
