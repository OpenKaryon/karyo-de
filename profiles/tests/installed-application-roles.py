#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host role visibility and fresh native Work acceptance."""
import hashlib, json, os, shutil, socket, stat, subprocess, sys, time, uuid
from pathlib import Path

if os.getuid() != 0 or os.geteuid() != 0 or socket.gethostname() not in {
        'karyo-de-acceptance', 'karyo-de-ubuntu-acceptance'} or sys.argv[1:]:
    raise SystemExit('Use only the fixed disposable acceptance hosts as root')
sys.dont_write_bytecode = True
sys.path.insert(0, '/usr/libexec/karyo/profiles')
from karyo_profiles.application_catalog import POLICY, view
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.logind import Sessions
from karyo_profiles.model import DesktopSettings
from karyo_profiles.storage import ConfigStore


def require(value, message):
    if not value: raise RuntimeError(message)


def run(*args):
    return subprocess.check_output(args, text=True, timeout=30).strip()


def wait_for(predicate, message):
    deadline = time.monotonic() + 45
    while not predicate():
        require(time.monotonic() < deadline, message)
        time.sleep(.1)


def journal():
    return json.loads(Path('/run/karyo/profile-host/state.json').read_text())


def request(source, target):
    result = subprocess.run(['/usr/local/libexec/karyo-qa-public-runtime', '--helper', source, target],
                            capture_output=True, text=True, timeout=65)
    require(result.returncode == 0, 'Public switch failed: ' + result.stderr)
    return json.loads(result.stdout)


def user(name, *args):
    uid = LocalAccounts().account(name).uid
    return ['/usr/sbin/runuser', '-u', name, '--', '/usr/bin/env', 'XDG_RUNTIME_DIR=/run/user/'+str(uid),
            'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus', *args]


def work(*args):
    return run(*user('kq-switch-work', '/usr/bin/systemd-run', '--user', '--wait', '--pipe', '--collect',
                     '--unit=karyo-qa-roles-'+uuid.uuid4().hex+'.service', '--', *args))


def environment(pid):
    return dict(os.fsdecode(item).split('=', 1) for item in Path('/proc',str(pid),'environ').read_bytes().split(b'\0') if b'=' in item)


# Exercise the installed GLib desktop resolver as an ordinary process as well
# as actual Karyo shell and Files environment, with no alternate launcher code.
GIO = """import ctypes,json
lib=ctypes.CDLL('libgio-2.0.so.0');obj=ctypes.CDLL('libgobject-2.0.so.0')
lib.g_desktop_app_info_new.argtypes=[ctypes.c_char_p];lib.g_desktop_app_info_new.restype=ctypes.c_void_p
lib.g_desktop_app_info_get_is_hidden.argtypes=[ctypes.c_void_p];lib.g_desktop_app_info_get_is_hidden.restype=ctypes.c_int
obj.g_object_unref.argtypes=[ctypes.c_void_p]
visible=[]
for name in ('home','work','common','never'):
 p=lib.g_desktop_app_info_new(('karyo-qa-role-'+name+'.desktop').encode())
 if p:
  if not lib.g_desktop_app_info_get_is_hidden(p):visible.append(name)
  obj.g_object_unref(p)
print(json.dumps(visible))
"""

store, sessions = ConfigStore(), Sessions()
source = next(row for row in sessions.all() if row.name == 'karyoqa' and row.uid == 1001 and row.active)
helper = Path('/usr/local/libexec/karyo-qa-public-runtime')
require(hashlib.sha256(helper.read_bytes()).hexdigest() ==
        '3be27e4ef96458f2400d7ea68058db6cdfe40916c4299ecdb68d152cedea1d83', 'Fixed context helper changed')
require(not journal()['profiles'], 'Prior runtime requires review')
root_paths = [POLICY, Path('/usr/local/libexec/karyo-qa-role-marker')]
root_paths += [Path(parent)/('karyo-qa-role-'+name+'.desktop') for parent in
               ('/usr/local/share/applications','/etc/xdg/autostart') for name in ('home','work','common','never')]
require(all(not os.path.lexists(path) for path in root_paths), 'Role fixture already exists')
run('/usr/local/libexec/karyo-qa-two-sessions', 'prepare')
original_policy = store.settings('kq-switch-work')
selected = DesktopSettings(workstation=True)
policy = {'version':1, **{section:{'karyo-qa-role-'+name+'.desktop': roles for name,roles in
    (('home',['home']),('work',['work']),('common',['home','work']),('never',[]))}
    for section in ('applications','autostart')}}
created = {}
home_view = None
graph = None
try:
    with store.locked():store.save_settings('kq-switch-work', selected)
    for path in root_paths:
        if path == POLICY:
            data = (json.dumps(policy)+'\n').encode();mode=0o644
        elif path.name == 'karyo-qa-role-marker':
            data = b'#!/bin/sh\nset -eu\ntest "$(id -u)" = 1002\ncase "$1" in home|work|common|never) ;; *) exit 2;; esac\ntest "$XDG_RUNTIME_DIR" = /run/user/1002\nexec /usr/bin/touch -- "$XDG_RUNTIME_DIR/karyo-qa-role-$1"\n';mode=0o755
        else:
            name=path.stem.removeprefix('karyo-qa-role-')
            data=('[Desktop Entry]\nType=Application\nName=Karyo QA '+name+'\nExec=/usr/local/libexec/karyo-qa-role-marker '+name+'\n').encode();mode=0o644
        fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,mode)
        try:os.fchmod(fd,mode);os.write(fd,data);os.fsync(fd)
        finally:os.close(fd)
        info=path.stat();created[str(path)]=(info.st_dev,info.st_ino,hashlib.sha256(data).hexdigest())
    home_view = Path(run(*user('karyoqa','/usr/bin/karyo-application-catalog')))
    home_identity=(home_view.stat().st_dev,home_view.stat().st_ino)
    require(home_view.parent == Path('/run/user/1001'), 'Home view escaped its runtime')
    require(run(*user('karyoqa','/usr/bin/karyo-application-catalog')) == str(home_view), 'Home view was not reused')
    visible=json.loads(run(*user('karyoqa','/usr/bin/env','XDG_DATA_DIRS='+str(home_view)+'/share:/usr/local/share:/usr/share',
                               '/usr/bin/python3','-I','-c',GIO)))
    require(visible == ['home','common'], 'Home resolver visibility differs')
    require(request('karyoqa','kq-switch-work').get('starting') is True, 'Work was not started cold')
    record=journal()['profiles'][0];graph=record['session']['graphical']
    def clients():
        result={}
        for item in Path('/proc').iterdir():
            if not item.name.isdecimal():continue
            try:
                if item.stat().st_uid == 1002:
                    name=(item/'comm').read_text().strip()
                    if name in ('karyo-shell','karyo-files'):result[name]=int(item.name)
            except (FileNotFoundError,ProcessLookupError):pass
        return result
    wait_for(lambda: set(clients())=={'karyo-shell','karyo-files'}, 'Work clients did not start')
    runtime=Path('/run/user/1002')
    wait_for(lambda: all((runtime/('karyo-qa-role-'+name)).is_file() for name in ('work','common')), 'Allowed autostart did not run')
    require(all(not (runtime/('karyo-qa-role-'+name)).exists() for name in ('home','never')), 'Hidden autostart ran')
    child_env=environment(clients()['karyo-shell'])
    prefix=Path(child_env['XDG_DATA_DIRS'].split(':')[0])
    require(prefix.parent.parent == runtime and prefix.name == 'share', 'Work shell missed private view')
    require(child_env['XDG_CONFIG_DIRS'].split(':')[0] == str(prefix.parent/'config'), 'Work config view differs')
    require(environment(clients()['karyo-files'])['XDG_DATA_DIRS'] == child_env['XDG_DATA_DIRS'], 'Files missed view')
    activation=dict(line.split('=',1) for line in run(*user('kq-switch-work','/usr/bin/systemctl','--user','show-environment')).splitlines() if '=' in line)
    require(all(activation[key] == child_env[key] for key in ('XDG_DATA_DIRS','XDG_CONFIG_DIRS')), 'Activation environment missed view')
    require(json.loads(work('/usr/bin/python3','-I','-c',GIO)) == ['work','common'], 'Work resolver visibility differs')
    require(not request('kq-switch-work','karyoqa').get('starting',False), 'Home was recreated')
    require(not request('karyoqa','kq-switch-work').get('starting',False), 'Work was recreated')
    require(journal()['profiles'][0]['token'] == record['token'], 'Switch replaced runtime')
    print(json.dumps({'cold_public_work':True,'home_visible':visible,'work_visible':['work','common'],
                      'native_autostart':['work','common'],'hidden_autostart':['home','never'],
                      'shell_files_manager_environment':True,'home_view_reused':True,'existing_switch_reused':True}),flush=True)
finally:
    if graph is not None and journal()['profiles']:
        work('/usr/bin/labwc','--exit')
        wait_for(lambda:not journal()['profiles'], 'Work cleanup did not finish')
    require(not journal()['profiles'], 'Retained runtime needs review; fixtures kept')
    require(sessions.get(source.id)==source, 'Original Home changed')
    if home_view is not None:
        require((home_view.stat().st_dev,home_view.stat().st_ino)==home_identity,'Home cache identity changed')
        code="import json,sys;from pathlib import Path;sys.path.insert(0,'/usr/libexec/karyo/profiles');from karyo_profiles.application_catalog import check_cache;check_cache(Path(sys.argv[1]),json.loads(sys.argv[2]))"
        run(*user('karyoqa','/usr/bin/python3','-I','-c',code,str(home_view),json.dumps(view(policy,'home'))))
        require(shutil.rmtree.avoids_symlink_attacks,'Symlink-safe cleanup unavailable');shutil.rmtree(home_view)
    for text,identity in reversed(list(created.items())):
        path=Path(text);info=path.lstat()
        require(stat.S_ISREG(info.st_mode) and (info.st_dev,info.st_ino,hashlib.sha256(path.read_bytes()).hexdigest())==identity,
                'Fixture changed; retain '+text)
        path.unlink()
    with store.locked():
        require(store.settings('kq-switch-work')==selected,'Work metadata changed')
        store.save_settings('kq-switch-work',original_policy)
    run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
require(not run('/usr/bin/dpkg','-V','karyo-profile-accounts','karyo-session-entry') and not run('/usr/bin/dpkg','--audit'),
        'Package integrity failed')
print('PASS installed role visibility/autostart; captured fixture cleanup and original Home retained',flush=True)
