#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host public VPN startup and ordinary client acceptance."""
import hashlib,json,os,shutil,signal,socket,stat,subprocess,sys,time,uuid
from pathlib import Path
if (os.getuid()!=0 or os.geteuid()!=0 or socket.gethostname() not in
        {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} or sys.argv[1:] not in ([],['--missing'],['--connected'],['--pkce'])):
    raise SystemExit('Use only the fixed disposable hosts as root')
sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.consoles import KernelConsoles
from karyo_profiles.journal_session import stat_start
from karyo_profiles.logind import Sessions
from karyo_profiles.model import DesktopSettings
from karyo_profiles.runtime_policy import enabled,forwarding_ready
from karyo_profiles.storage import ConfigStore


def require(value,message):
    if not value:raise RuntimeError(message)


def run(*args):return subprocess.check_output(args,text=True,timeout=25).strip()


def snapshot():
    return {k:run(*a) for k,a in {
        'routes':('/usr/sbin/ip','-4','-j','-N','route','show','table','all'),
        'rules':('/usr/sbin/ip','-4','-j','-N','rule','show'),
        'nft':('/usr/sbin/nft','-j','list','ruleset')}.items()}|{
        'resolver':os.readlink('/etc/resolv.conf'),
        'resolver_sha':hashlib.sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest()}


def journal():
    p=Path('/run/karyo/profile-host/state.json');s=p.lstat()
    require(stat.S_ISREG(s.st_mode) and s.st_uid==0 and stat.S_IMODE(s.st_mode)==0o600 and s.st_nlink==1,'Unsafe journal')
    return json.loads(p.read_bytes())


def wait_for(predicate,message,seconds=40):
    deadline=time.monotonic()+seconds
    while not predicate():
        require(time.monotonic()<deadline,message);time.sleep(.1)


def request(source,target):
    return subprocess.run(['/usr/local/libexec/karyo-qa-public-runtime','--helper',source,target],capture_output=True,text=True,timeout=65)


def switch(source,target):
    result=request(source,target);require(result.returncode==0,'Public switch failed: '+result.stderr)
    value=json.loads(result.stdout);require(value['user']==target and value['changed'],'Wrong public target');return value


def user(*args):
    return ['/usr/sbin/runuser','-u','kq-switch-work','--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/1002',
            'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus',*args]


def cli(*args):
    name='karyo-qa-public-netbird-'+uuid.uuid4().hex+'.service'
    return subprocess.run(user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect','--unit='+name,'--',
                               '/usr/bin/karyo-netbird-cli',*args),capture_output=True,text=True,timeout=70)


def status():
    result=cli('status','--json');require(result.returncode==0,'Ordinary Work CLI failed')
    return json.loads(result.stdout)


missing=sys.argv[1:]==['--missing'];pkce=sys.argv[1:]==['--pkce'];connected=sys.argv[1:] in (['--connected'],['--pkce'])
helper=Path('/usr/local/libexec/karyo-qa-public-runtime');info=helper.lstat()
require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and not info.st_mode&0o022 and
        hashlib.sha256(helper.read_bytes()).hexdigest()=='3be27e4ef96458f2400d7ea68058db6cdfe40916c4299ecdb68d152cedea1d83','Fixed context helper changed')
require(enabled(),'Explicit runtime activation required');forwarding_ready()
require(not journal()['profiles'] and not run('/usr/sbin/ip','netns','list'),'Prior runtime requires review')
require(not os.path.lexists('/var/lib/karyo/profile-vpn/1002'),'Prior provider state requires review')
require(not os.path.lexists('/usr/bin/netbird') if missing else Path('/usr/bin/netbird').is_file(),'Unexpected vendor fixture')
store,accounts,sessions=ConfigStore(),LocalAccounts(),Sessions()
source=next(s for s in sessions.all() if s.name=='karyoqa' and s.uid==1001 and s.active)
original=snapshot();original_accounts={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [Path('/etc/passwd'),Path('/etc/shadow'),Path('/etc/group'),Path('/etc/gshadow'),*Path('/etc/pam.d').glob('common-*')]}
run('/usr/local/libexec/karyo-qa-two-sessions','prepare');child=accounts.account('kq-switch-work');require(child.uid==1002,'Wrong child')
original_policy=store.settings(child.name);policy=DesktopSettings(vpn='netbird')
state_identity=keyfile=key_identity=key=None
with store.locked():store.save_settings(child.name,policy)
try:
    if missing:
        before=snapshot();result=request('karyoqa',child.name)
        require(result.returncode==1 and 'optional Karyo NetBird provider and client packages' in result.stderr,
                'Public missing-provider response is not actionable')
        require(not journal()['profiles'] and snapshot()==before and not run('/usr/sbin/ip','netns','list'),
                'Missing provider allocated resources')
        require(not os.path.lexists('/var/lib/karyo/profile-vpn/1002') and not os.path.lexists('/run/user/1002'),
                'Missing provider prepared a manager or provider state')
        print(json.dumps({'missing_provider':'rejected before allocation','public_message':'actionable'}),flush=True)
    else:
        modes=('logout','crash','locked-crash') if not connected else ('logout',)
        previous_service=None
        for mode in modes:
            selected=DesktopSettings(vpn='netbird',password='home' if mode=='locked-crash' else 'none')
            with store.locked():store.save_settings(child.name,selected)
            cold=switch('karyoqa',child.name);require(cold.get('starting') is True,'Public startup did not create Work')
            data=journal();require(data['schema']==7 and len(data['profiles'])==1,'Wrong public journal')
            record=data['profiles'][0];provider=record['provider'];graph=record['session']['graphical']
            require(provider['kind']=='netbird' and provider['pending'] is None and record['network']['contents']=='tunnel'
                    and record['session']['catalog']=='netbird' and 'forwarding' in record,'Public graph lacks captured provider/catalog')
            identity=tuple(provider['state']['directory'])
            if state_identity is None:state_identity=identity
            else:require(state_identity==identity,'Provider persistent state changed between cold launches')
            native=provider['service']['unit'];require(previous_service!=native['invocation'],'Cold launch reused old service')
            previous_service=native['invocation']
            value=status();require(value.get('daemonStatus')=='NeedsLogin','Fresh public provider is not awaiting login')
            env=Path('/proc',str(record['session']['manager']['live']['pid']),'environ').read_bytes().split(bytes([0]))
            require(b'XDG_DATA_DIRS=/usr/share/karyo/profile-catalog/netbird:/usr/local/share:/usr/share' in env
                    and any(e.startswith(b'KARYO_NETBIRD_ENDPOINT=') for e in env),'Public manager missed provider environment')
            home=subprocess.run(['/usr/sbin/runuser','-u','karyoqa','--','/usr/bin/karyo-netbird-cli','status','--json'],capture_output=True,text=True,timeout=10)
            require(home.returncode==1,'Unscoped Home client reached Work provider')
            if pkce:
                subprocess.run(['/usr/local/libexec/karyo-qa-netbird-pkce'],check=True,timeout=240)
                require(status().get('daemonStatus')=='Connected','Real browser PKCE did not enroll Work')
            elif connected:
                admission=Path('/var/lib/karyo-qa-netbird-admission/setup-key');s=admission.lstat()
                require(stat.S_ISREG(s.st_mode) and s.st_uid==s.st_gid==0 and stat.S_IMODE(s.st_mode)==0o600 and s.st_nlink==1 and s.st_size<65,'Unsafe lab admission')
                key=admission.read_bytes();uuid.UUID(key.decode().strip());keyfile=Path(child.home)/'.karyo-qa-public-setup-key'
                fd=os.open(keyfile,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
                try:os.fchown(fd,1002,1002);os.write(fd,key);os.fsync(fd)
                finally:os.close(fd)
                key_identity=(keyfile.stat().st_dev,keyfile.stat().st_ino)
                result=cli('up','--management-url','http://192.168.230.163:18443','--setup-key-file',str(keyfile),'--hostname','karyo-qa-public-'+socket.gethostname())
                require(result.returncode==0 and status().get('daemonStatus')=='Connected','Public ordinary enrollment failed')
            if mode=='locked-crash':
                wait_for(lambda:sessions.get(graph['login']['id']).locked,'Protected public Work did not lock')
            else:
                require(not switch(child.name,'karyoqa').get('starting',False),'Home recreated')
                require(not switch('karyoqa',child.name).get('starting',False),'Running Work recreated')
                current=journal()['profiles'][0]
                require(current['token']==record['token'] and current['provider']==record['provider'],'Existing switch replaced provider')
            if mode=='logout':
                run(*user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect','--unit=karyo-qa-public-vpn-logout.service','--','/usr/bin/labwc','--exit'))
            else:
                fd=os.pidfd_open(graph['pid'])
                try:
                    require(stat_start(graph['pid'])==graph['start'],'Captured compositor start changed')
                    exe=os.stat('/proc/'+str(graph['pid'])+'/exe');require([exe.st_dev,exe.st_ino]==graph['compositor'],'Captured compositor executable changed')
                    signal.pidfd_send_signal(fd,signal.SIGKILL)
                finally:os.close(fd)
            wait_for(lambda:not journal()['profiles'],'Public provider supervisor did not clean up')
            require(sessions.get(source.id)==source and snapshot()==original,'Original Home/network changed')
            with KernelConsoles() as consoles:require(consoles.state().active==source.vt,'Public provider did not return Home')
            print(json.dumps({'mode':mode,'cold_public_provider':True,'ordinary_status':value.get('daemonStatus'),
                              'connected':connected,'browser_pkce':pkce,'state_reused':identity==state_identity,'existing_switch_unchanged':True,
                              'home_return':True,'cleanup':'complete'}),flush=True)
            with store.locked():store.save_settings(child.name,policy)
finally:
    require(not journal()['profiles'],'Retained public resources require review; child kept')
    with store.locked():
        require(store.settings(child.name)==policy,'QA policy changed; retain account')
        store.save_settings(child.name,original_policy)
    if keyfile is not None:
        require((keyfile.stat().st_dev,keyfile.stat().st_ino)==key_identity and keyfile.read_bytes()==key,'Lab key changed')
        keyfile.unlink()
    if state_identity is not None:
        state=Path('/var/lib/karyo/profile-vpn/1002/netbird')
        require((state.stat().st_dev,state.stat().st_ino)==state_identity and
                json.loads((state/'owner.json').read_bytes())['account']['name']==child.name,'Provider fixture identity changed')
        require(shutil.rmtree.avoids_symlink_attacks,'Native symlink-safe cleanup required')
        shutil.rmtree(state);state.parent.rmdir()
    run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
require({p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in original_accounts}==original_accounts,'Accounts/PAM changed')
require(snapshot()==original and sessions.get(source.id)==source and not run('/usr/sbin/ip','netns','list'),'Original host changed')
require(not run('/usr/bin/dpkg','-V','karyo-profile-accounts','karyo-profile-host','karyo-profile-runtime','karyo-profile-netbird-client') and not run('/usr/bin/dpkg','--audit'),'Package integrity failed')
print('PASS installed public VPN startup; original Home/network/accounts retained',flush=True)
