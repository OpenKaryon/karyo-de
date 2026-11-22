#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Installed protocol acceptance on the two disposable native QA hosts only."""
import hashlib,json,os,pwd,socket,stat,subprocess,sys
from pathlib import Path

if os.getuid()!=os.geteuid() or os.getuid()!=0 or socket.gethostname() not in {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} or sys.argv[1:]:
    raise SystemExit('Use only the two fixed disposable QA hosts as root')
STATE=Path('/var/lib/karyo-qa/xclipboard.json')
TESTS=Path('/usr/local/libexec/karyo-qa-xclipboard-tests')
ACCOUNT=pwd.getpwnam('qaadmin')
BASE='debian' if socket.gethostname()=='karyo-de-acceptance' else 'ubuntu'
SHELL,SESSION={'debian':(288593,'1631'),'ubuntu':(406670,'1808')}[BASE]

def require(value,message):
    if not value:raise RuntimeError(message)

def run(*args):return subprocess.check_output(args,text=True,timeout=20).strip()

def checksum(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def snapshot():
    paths=[Path('/etc/passwd'),Path('/etc/group'),Path('/etc/shadow'),Path('/etc/gshadow')]
    for folder in ('/etc/pam.d','/etc/lightdm'):
        paths.extend(p for p in Path(folder).rglob('*') if p.is_file())
    return {str(p):checksum(p) for p in paths}

def check_home():
    require(Path('/proc',str(SHELL),'comm').read_text().strip()=='karyo-shell','Original Home shell changed')
    value=dict(line.split('=',1) for line in run('/usr/bin/loginctl','show-session',SESSION,'-p','Name','-p','Active','-p','VTNr','-p','LockedHint').splitlines())
    require(value=={'Name':'karyoqa','Active':'yes','VTNr':'7','LockedHint':'no'},'Original Home session changed')

require(not os.path.lexists(STATE),'Previous installed clipboard acceptance requires review')
info=TESTS.lstat();require(stat.S_ISDIR(info.st_mode) and info.st_uid==0 and stat.S_IMODE(info.st_mode)==0o755,'Unsafe test directory')
files={}
for name in ('test_bridge.py','test_owner.py'):
    p=TESTS/name;info=p.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and info.st_nlink==1 and stat.S_IMODE(info.st_mode)==0o644,'Unsafe native test')
    files[name]=checksum(p)
require(ACCOUNT.pw_uid==ACCOUNT.pw_gid==1000,'QA account changed')
check_home();before=snapshot();dm=os.readlink('/etc/systemd/system/display-manager.service')
fd=os.open(STATE,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd)
value={'phase':'testing','base':BASE,'home_shell':SHELL,'home_session':SESSION,'tests':files,'accounts':before,'dm':dm}
def save():
    with STATE.open('w') as stream:json.dump(value,stream);stream.flush();os.fsync(stream.fileno())
save()
env=['/usr/bin/env','-u','DISPLAY','-u','WAYLAND_DISPLAY','-u','XAUTHORITY','-u','DBUS_SESSION_BUS_ADDRESS',
     'HOME='+ACCOUNT.pw_dir,'PYTHONDONTWRITEBYTECODE=1','KARYO_XCLIPBOARD_TEST_MODULES=/usr/libexec/karyo/clipboard']
ordinary=['/usr/bin/setpriv','--reuid=1000','--regid=1000','--init-groups','--no-new-privs',
          '--bounding-set=-all','--inh-caps=-all','--ambient-caps=-all',*env]
try:
    require(not run('/usr/bin/dpkg','-V','karyo-xclipboard'),'Installed package differs')
    require(not run('/usr/bin/dpkg','--audit'),'Package audit failed')
    root=subprocess.run(['/usr/bin/karyo-xclipboard'],capture_output=True,text=True,timeout=5)
    require(root.returncode!=0 and 'ordinary desktop account' in root.stderr,'Privileged invocation accepted')
    absent=subprocess.run([*ordinary,'/usr/bin/karyo-xclipboard'],capture_output=True,text=True,timeout=5)
    require(absent.returncode==1 and 'needs both DISPLAY and WAYLAND_DISPLAY' in absent.stderr,'Missing native display was accepted')
    result=subprocess.run([*ordinary,'/usr/bin/python3','-m','unittest','discover','-s',str(TESTS),'-v'],
                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
    print(result.stdout,flush=True)
    require(result.returncode==0 and 'Ran 21 tests' in result.stdout and 'skipped' not in result.stdout,'Installed protocol gate failed or skipped')
    for path in Path('/proc').iterdir():
        if not path.name.isdecimal():continue
        try:
            args=(path/'cmdline').read_bytes().split(bytes([0]))
            require(not (path.stat().st_uid==1000 and any(b'/usr/libexec/karyo/clipboard/karyo_xclipboard/' in arg for arg in args)),'QA clipboard process remains')
        except (FileNotFoundError,ProcessLookupError):pass
    require(snapshot()==before and os.readlink('/etc/systemd/system/display-manager.service')==dm,'Accounts/PAM/DM changed')
    check_home()
    require(not run('/usr/bin/dpkg','-V','karyo-xclipboard') and not run('/usr/bin/dpkg','--audit'),'Package integrity changed')
    value['phase']='accepted';save()
finally:
    for name,digest in files.items():
        p=TESTS/name;require(checksum(p)==digest,'Native QA fixture changed');p.unlink()
    TESTS.rmdir()
    value['fixtures_removed']=True;save()
require(value['phase']=='accepted','Native clipboard remains unaccepted')
print('PASS installed X11/Wayland protocol:21 ordinary tests; original Home/account/PAM/DM retained',flush=True)
