#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cold LightDM Home role lifecycle on two fixed disposable acceptance hosts."""
import hashlib,json,os,pwd,shutil,socket,stat,subprocess,sys,time
from pathlib import Path

if os.getuid()!=0 or os.geteuid()!=0 or socket.gethostname() not in {
        'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} or sys.argv[1:]:
    raise SystemExit('Use only the fixed disposable acceptance hosts as root')
sys.dont_write_bytecode=True
sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.application_catalog import view
from karyo_profiles.journal_session import stat_start
from karyo_profiles.logind import Sessions
from karyo_profiles.model import DesktopSettings
from karyo_profiles.storage import ConfigStore


def require(value,message):
    if not value:raise RuntimeError(message)


def run(*args):return subprocess.check_output(args,text=True,timeout=35).strip()


def wait_for(predicate,message,seconds=55):
    deadline=time.monotonic()+seconds
    while not predicate():
        require(time.monotonic()<deadline,message);time.sleep(.1)


def processes():
    found={}
    for path in Path('/proc').iterdir():
        if not path.name.isdecimal():continue
        try:
            if path.stat().st_uid==1001:
                name=(path/'comm').read_text().strip()
                found.setdefault(name,[]).append(int(path.name))
        except (FileNotFoundError,ProcessLookupError):pass
    return found


def environment(pid):
    return dict(os.fsdecode(row).split('=',1) for row in Path('/proc',str(pid),'environ').read_bytes().split(b'\0') if b'=' in row)


def user(*args):
    return ['/usr/sbin/runuser','-u','karyoqa','--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/1001',
            'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus',*args]


STATE=Path('/var/lib/karyo-qa/cold-home-roles.json')
DM=Path('/etc/lightdm/lightdm.conf.d/99-karyo-qa-cold-home.conf')
POLICY=Path('/etc/karyo/application-roles.json')
MARKER=Path('/usr/local/libexec/karyo-qa-cold-home-marker')
DATA={'version':1,**{section:{'karyo-qa-cold-'+name+'.desktop':roles for name,roles in
    (('home',['home']),('work',['work']),('common',['home','work']),('never',[]))}
    for section in ('applications','autostart')}}
FIXTURES={DM:b'[Seat:*]\nautologin-user=karyoqa\nautologin-user-timeout=0\nautologin-session=karyo\n',
          POLICY:(json.dumps(DATA)+'\n').encode(),
          MARKER:b'#!/bin/sh\nset -eu\ntest "$(id -u)" = 1001\ntest "$XDG_RUNTIME_DIR" = /run/user/1001\ncase "$1" in home|work|common|never) ;; *) exit 2;; esac\nexec /usr/bin/touch -- "$XDG_RUNTIME_DIR/karyo-qa-cold-$1"\n'}
for parent in ('/usr/local/share/applications','/etc/xdg/autostart'):
    for name in ('home','work','common','never'):
        FIXTURES[Path(parent)/('karyo-qa-cold-'+name+'.desktop')]=('[Desktop Entry]\nType=Application\nName=Karyo cold '+name+'\nExec='+str(MARKER)+' '+name+'\n').encode()


def save():
    with STATE.open('w') as stream:json.dump(record,stream);stream.flush();os.fsync(stream.fileno())


def check_fixture(path):
    info=path.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and info.st_nlink==1 and
            [info.st_dev,info.st_ino]==record['files'][str(path)] and path.read_bytes()==FIXTURES[path],
            'Captured fixture changed: '+str(path))


def cold_start(label):
    before=processes();require(len(before.get('labwc',[]))==1,'Original compositor ambiguous')
    old=before['labwc'][0];old_start=stat_start(old)
    record['phase']=label+'-stopping';record['prior_compositor']=[old,old_start];save()
    run('/usr/bin/systemctl','stop','lightdm.service')
    wait_for(lambda:not processes(),'Original Home user processes did not end',40)
    require(not any(row.uid==1001 for row in Sessions().all()),'Original Home login/manager still exists')
    require(not Path('/run/user/1001').exists(),'Original runtime was retained')
    record['phase']=label+'-starting';save()
    run('/usr/bin/systemctl','start','lightdm.service')
    def ready():
        current=processes()
        return all(len(current.get(name,[]))==1 for name in ('labwc','karyo-shell','karyo-files')) and any(
            row.uid==1001 and row.name=='karyoqa' and row.active and not row.locked for row in Sessions().all())
    wait_for(ready,'Fresh LightDM Karyo Home did not become ready')
    current=processes();compositor=current['labwc'][0]
    require(compositor!=old or stat_start(compositor)!=old_start,'Home compositor was reused')
    row=next(row for row in Sessions().all() if row.uid==1001 and row.active)
    record['homes'].append({'label':label,'compositor':compositor,'start':stat_start(compositor),
        'shell':current['karyo-shell'][0],'files':current['karyo-files'][0],'session':row.id,'vt':row.vt})
    record['phase']=label+'-ready';save()
    return current,row


require(pwd.getpwnam('karyoqa').pw_uid==1001,'Wrong QA Home identity')
require(not os.path.lexists(STATE) and all(not os.path.lexists(path) for path in FIXTURES),'Prior fixture requires review')
require(not json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles'],'Prior runtime requires review')
require(not Path('/etc/karyo/profile-runtime.conf').exists(),'Optional runtime must be inactive')
require(run('/usr/bin/systemctl','is-active','lightdm.service')=='active','Original display manager is not active')
require(not run('/usr/bin/dpkg','-V','karyo-session-entry','karyo-profile-accounts'),'Installed packages changed')
require(not list(Path('/run/user/1001').glob('karyo-applications-*')),'Prior Home catalog requires review')
tracked=[Path('/etc/passwd'),Path('/etc/shadow'),Path('/etc/group'),Path('/etc/gshadow'),*Path('/etc/pam.d').glob('common-*')]
original={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}
manager=os.readlink('/etc/systemd/system/display-manager.service')
config={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in Path('/etc/lightdm').rglob('*') if path.is_file()}
record={'phase':'prepared','files':{},'homes':[],'accounts':original,'dm':manager,'config':config}
fd=os.open(STATE,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd);save()
run('/usr/local/libexec/karyo-qa-two-sessions','prepare')
store=ConfigStore();baseline=store.settings('kq-switch-work')
require(baseline==DesktopSettings(),'Unexpected child settings')
with store.locked():store.save_settings('kq-switch-work',DesktopSettings(workstation=True))
for path,data in FIXTURES.items():
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o755 if path==MARKER else 0o644)
    try:os.write(fd,data);os.fsync(fd)
    finally:os.close(fd)
    info=path.stat();record['files'][str(path)]=[info.st_dev,info.st_ino];save()
current,row=cold_start('roles')
runtime=Path('/run/user/1001')
wait_for(lambda:all((runtime/('karyo-qa-cold-'+name)).is_file() for name in ('home','common')),'Home allowed autostart missing')
require(all(not (runtime/('karyo-qa-cold-'+name)).exists() for name in ('work','never')),'Home hidden autostart executed')
env=environment(current['karyo-shell'][0]);catalog=Path(env['XDG_DATA_DIRS'].split(':')[0]).parent
require(catalog.parent==runtime and catalog.name.startswith('karyo-applications-'),'Fresh Home has no private catalog')
require(env['XDG_CONFIG_DIRS'].split(':')[0]==str(catalog/'config'),'Fresh Home config catalog missing')
require(environment(current['karyo-files'][0])['XDG_DATA_DIRS']==env['XDG_DATA_DIRS'],'Fresh Home Files missed catalog')
activation=dict(line.split('=',1) for line in run(*user('/usr/bin/systemctl','--user','show-environment')).splitlines() if '=' in line)
require(all(activation[key]==env[key] for key in ('XDG_DATA_DIRS','XDG_CONFIG_DIRS')),'Fresh Home activation missed catalog')
for path in (catalog/'share/applications/karyo-qa-cold-work.desktop',catalog/'share/applications/karyo-qa-cold-never.desktop'):
    require(b'Hidden=true' in path.read_bytes(),'Home mask missing')
record['role_acceptance']={'native_autostart':['home','common'],'hidden_autostart':['work','never'],
    'shell_files_activation':True,'fresh_manager':True,'catalog':str(catalog)};save()
check_fixture(POLICY);POLICY.unlink();del record['files'][str(POLICY)];record['phase']='policy-removed';save()
current,row=cold_start('without-policy')
require(not list(runtime.glob('karyo-applications-*')),'Old catalog survived native runtime teardown')
for name in ('karyo-shell','karyo-files'):
    env=environment(current[name][0])
    require('karyo-applications-' not in env.get('XDG_DATA_DIRS','') and 'karyo-applications-' not in env.get('XDG_CONFIG_DIRS',''),
            'Stale catalog environment survived in '+name)
activation=run(*user('/usr/bin/systemctl','--user','show-environment'))
require('karyo-applications-' not in activation,'Stale catalog survived activation environment')
wait_for(lambda:all((runtime/('karyo-qa-cold-'+name)).is_file() for name in ('home','work','common','never')),
         'Native autostart did not return to policy-free behavior')
for name in ('home','work','common','never'):
    marker=runtime/('karyo-qa-cold-'+name);info=marker.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_uid==1001 and info.st_nlink==1 and info.st_size==0,'Marker changed')
    marker.unlink()
for text in reversed(list(record['files'])):
    path=Path(text);check_fixture(path);path.unlink();del record['files'][text];save()
with store.locked():
    require(store.settings('kq-switch-work')==DesktopSettings(workstation=True),'Child policy changed')
    store.save_settings('kq-switch-work',baseline)
run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
require(all(hashlib.sha256(Path(text).read_bytes()).hexdigest()==digest for text,digest in original.items()),'Account/PAM bytes changed')
require(os.readlink('/etc/systemd/system/display-manager.service')==manager,'Display manager changed')
require({str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in Path('/etc/lightdm').rglob('*') if path.is_file()}==config,
        'LightDM configuration not restored')
require(not run('/usr/bin/dpkg','-V','karyo-session-entry','karyo-profile-accounts') and not run('/usr/bin/dpkg','--audit'),'Package audit failed')
record['phase']='complete';record['cleanup']='role fixtures, child and temporary LightDM autologin removed';save()
print(json.dumps({'homes':record['homes'],'roles':record['role_acceptance'],'policy_removal':'no stale catalog or environment',
    'cleanup':record['cleanup'],'new_baseline':record['homes'][-1],'accounts_pam_dm':'unchanged'}),flush=True)
