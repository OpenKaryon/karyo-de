#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise real native Qt dialogs without launching Wine or creating a prefix."""
import argparse,json,os,pwd,sys
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--installed',action='store_true');args=parser.parse_args()
sys.dont_write_bytecode=True
sys.path.insert(0,'/usr/libexec/karyo/win32' if args.installed else str(Path(__file__).resolve().parents[1]))
from karyo_win32 import launcher
from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import QFileDialog,QMessageBox
if os.getuid()==0:raise RuntimeError('Use an ordinary QA account')
home=Path(pwd.getpwuid(os.getuid()).pw_dir)
storage=home/'.local/share/karyo/windows'
if storage.exists():raise RuntimeError('GUI-only QA requires no existing Windows storage')
app=launcher.qt_application()
assert app.desktopFileName()=='karyo-win32','Qt window has no installed desktop identity'
assert not app.windowIcon().isNull(),'Qt window has no fallback icon'
seen=[];failures=[]
def dismiss(expected):
    widgets=[w for w in app.topLevelWidgets() if isinstance(w,expected) and w.isVisible()]
    if not widgets:
        failures.append('Expected real visible '+expected.__name__)
        app.closeAllWindows();return
    dialog=widgets[0];seen.append(type(dialog).__name__)
    if isinstance(dialog,QFileDialog):
        if Path(dialog.directory().absolutePath())!=home:failures.append('Picker trusted a foreign HOME')
        dialog.reject()
    else:
        if 'not-a-windows-program' not in dialog.text():failures.append('Error message lost file context')
        dialog.accept()
os.environ['HOME']='/not-the-real-home'
QTimer.singleShot(100,lambda:dismiss(QFileDialog))
assert launcher.main([])==0,'Cancel must succeed'
QTimer.singleShot(100,lambda:dismiss(QMessageBox))
assert launcher.main(['--gui',str(home/'not-a-windows-program.exe')])==1,'Invalid file must fail'
assert not failures,failures
assert seen==['QFileDialog','QMessageBox'],seen
assert not storage.exists(),'GUI-only path created a prefix'
print(json.dumps({'accepted':True,'uid':os.getuid(),'installed':args.installed,'dialogs':seen,'desktop_identity':app.desktopFileName(),'no_prefix':True}))
