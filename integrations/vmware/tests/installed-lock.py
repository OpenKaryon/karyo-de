#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Joined real PAM/Wayland lock and VMware ownership on fixed disposable hosts."""
import fcntl,hashlib,json,os,pwd,socket,stat,subprocess,sys,time
from pathlib import Path
if os.getuid()!=os.geteuid() or os.getuid()!=0 or socket.gethostname() not in {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} or sys.argv[1:]:
    raise SystemExit('Use only the fixed disposable native QA hosts')
sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.journal_session import stat_start
from karyo_profiles.logind import Sessions,process_session
from karyo_profiles.provision import LocalBackend
from karyo_profiles.model import DesktopSettings
from karyo_profiles.storage import ConfigStore
BASE='debian' if socket.gethostname()=='karyo-de-acceptance' else 'ubuntu'
HOME='karyoqa';WORK='kq-switch-work'
SHELL,LOGIN={'debian':(288593,'1631'),'ubuntu':(406670,'1808')}[BASE]
STATE=Path('/var/lib/karyo-qa/vmware-lock.json');ACK=Path('/var/lib/karyo-qa/vmware-lock-ack')
RUNTIME=Path('/run/karyo-vmware');CHANNEL=RUNTIME/'channel'
accounts=LocalAccounts();store=ConfigStore()

def require(value,message):
    if not value:raise RuntimeError(message)

def run(*args):return subprocess.check_output(args,text=True,timeout=35).strip()

def digest_password(name):return hashlib.sha256(accounts.password_hash(name).encode()).hexdigest()

def save():
    with STATE.open('w') as out:json.dump(receipt,out);out.flush();os.fsync(out.fileno())

def phase(value):
    receipt['phase']=value;save();print('QA_PHASE '+value,flush=True)

def wait_for(function,message,seconds=45):
    until=time.monotonic()+seconds
    while True:
        result=function()
        if result:return result
        require(time.monotonic()<until,message);time.sleep(.12)

def acknowledge(label):
    def ready():
        info=ACK.lstat();require([info.st_dev,info.st_ino]==receipt['ack'],'Acknowledgement identity changed')
        require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and stat.S_IMODE(info.st_mode)==0o600,'Unsafe acknowledgement')
        with ACK.open('rb') as stream:data=stream.read(128)
        return data==label.encode()+b'\n'
    wait_for(ready,'GUI acknowledgement missing',seconds=60)

def processes(name,uid=1002):
    result=[]
    for p in Path('/proc').iterdir():
        if not p.name.isdecimal():continue
        try:
            if p.stat().st_uid!=uid:continue
            args=(p/'cmdline').read_bytes().split(bytes([0]))
            if not args:continue
            match=(name=='agent' and b'/usr/bin/karyo-vmware-agent' in args or name=='bridge' and b'/usr/bin/karyo-xclipboard' in args
                   or name=='vmusr' and args[0].rsplit(b'/',1)[-1]==b'vmtoolsd' and b'vmusr' in args
                   or name in {'karyo-lock','karyo-shell'} and args[0].rsplit(b'/',1)[-1]==name.encode())
            if match:result.append(int(p.name))
        except (FileNotFoundError,ProcessLookupError):pass
    return sorted(result)

def graph():return json.loads(Path('/run/karyo/profile-host/state.json').read_bytes())['profiles']

def identity(pid):return {'pid':pid,'start':stat_start(pid)}

def owned_worker(pid):
    fields=dict(line.split(':',1) for line in Path('/proc',str(pid),'status').read_text().splitlines() if ':' in line)
    require(all(int(v)==1002 for v in fields['Uid'].split()) and fields['NoNewPrivs'].strip()=='1','Unexpected worker identity')
    require(all(int(fields[k],16)==0 for k in ('CapEff','CapPrm','CapInh','CapAmb')),'Worker retained privilege')
    require(any((os.stat(p).st_dev,os.stat(p).st_ino)==(CHANNEL.stat().st_dev,CHANNEL.stat().st_ino)
                for p in Path('/proc',str(pid),'fd').iterdir()),'Worker lost channel FD')
    return identity(pid)

def workers():
    groups={name:processes(name) for name in ('agent','vmusr','bridge')}
    if any(len(rows)!=1 for rows in groups.values()):return None
    try:return {name:owned_worker(rows[0]) for name,rows in groups.items()}
    except (FileNotFoundError,ProcessLookupError):return None

def no_workers():return not processes('vmusr') and not processes('bridge')

def free_channel():
    started=time.monotonic()
    def available():
        fd=os.open(CHANNEL,os.O_RDONLY|os.O_NOFOLLOW)
        try:
            try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB);return True
            except BlockingIOError:return False
        finally:os.close(fd)
    wait_for(available,'Coordinator did not finish releasing its channel',seconds=10)
    receipt.setdefault('channel_release_waits',[]).append(time.monotonic()-started)

def password(value,encrypted=False):
    result=subprocess.run(['/usr/sbin/chpasswd',*(['--encrypted'] if encrypted else ['--crypt-method','YESCRYPT'])],
        input=WORK+':'+value+'\n',text=True,capture_output=True,timeout=5)
    require(result.returncode==0,'Temporary Work credential update failed')

def check_locked(initial):
    require(Sessions().get(work_login).locked and len(processes('karyo-lock'))==1 and no_workers(),'Locked GUI retained VMware workers')
    if initial:require(not processes('agent') and not processes('karyo-shell'),'Initial PAM gate launched clients')
    else:require(processes('agent')==[first['agent']['pid']],'Manual lock replaced its coordinator')
    free_channel()

request=sys.stdin.buffer.readline(1025)
require(len(request)<=1024 and request.endswith(b'\n'),'Bounded credential packet required')
request=json.loads(request);require(set(request)=={'password'},'Unknown QA request')
secret=request.pop('password');require(isinstance(secret,str) and len(secret)==24 and all(c in '0123456789abcdef' for c in secret),'Invalid temporary QA secret')
require(not os.path.lexists(STATE) and not os.path.lexists(ACK) and not os.path.lexists(RUNTIME),'Prior lock fixture requires review')
require(not graph() and not processes('agent',1001) and not processes('vmusr',1001) and not processes('bridge',1001),'Prior native tools require review')
source=Sessions().get(LOGIN);require(source.uid==1001 and source.name==HOME and source.active and not source.locked,'Original Home differs')
require(Path('/proc',str(SHELL),'comm').read_text().strip()=='karyo-shell','Original Home shell changed')
receipt={'phase':'preparing','base':BASE,'home_session':LOGIN,'home_password':digest_password(HOME),'runtime':{},'observations':[],'child_password':None}
fd=os.open(STATE,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd)
fd=os.open(ACK,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd);receipt['ack']=[ACK.stat().st_dev,ACK.stat().st_ino];save()
prepared=changed=policy_changed=False;work_graph=None
try:
    run('/usr/local/libexec/karyo-qa-two-sessions','prepare');prepared=True
    require(accounts.account(WORK).uid==1002 and digest_password(WORK)==receipt['home_password'],'Unexpected captured child')
    password(secret);changed=True;receipt['child_password']=digest_password(WORK);save()
    with store.locked():
        require(store.settings(WORK)==DesktopSettings(),'Fresh child policy differs')
        store.save_settings(WORK,DesktopSettings(password='separate'));policy_changed=True
    run('/usr/bin/systemd-tmpfiles','--create','/usr/lib/tmpfiles.d/karyo-vmware.conf')
    for p in (RUNTIME,CHANNEL):receipt['runtime'][str(p)]=[p.stat().st_dev,p.stat().st_ino]
    result=subprocess.run(['/usr/local/libexec/karyo-qa-public-runtime','--helper',HOME,WORK],capture_output=True,text=True,timeout=65)
    require(result.returncode==0 and json.loads(result.stdout)['starting'],'Public protected Work startup failed')
    work_graph=wait_for(lambda:(graph()[0]['session'].get('graphical') if graph() else None),'Missing captured Work graphical lease')
    work_login=str(work_graph['login']['id']);receipt['work_login']=work_login;receipt['graphical']=work_graph;save()
    wait_for(lambda:Sessions().get(work_login).locked and processes('karyo-lock'),'No real initial lock')
    check_locked(True);receipt['observations'].append('initial-pam-suppressed-all-tools');phase('initial-locked')
    acknowledge('initial-wrong');time.sleep(3)
    check_locked(True);receipt['observations'].append('wrong-initial-kept-tools-closed');phase('initial-wrong-confirmed')
    wait_for(lambda:not Sessions().get(work_login).locked,'Initial PAM did not unlock',seconds=60)
    first=wait_for(workers,'Unlocked Work did not start its worker pair',seconds=60)
    require(process_session(first['agent']['pid'])==work_login,'Coordinator escaped native Work login')
    shell=processes('karyo-shell');require(len(shell)==1,'Work shell missing')
    receipt['workers_before_lock']=first;receipt['shell_before_lock']=shell;receipt['observations'].append('correct-initial-enabled-native-tools');phase('running')
    wait_for(lambda:Sessions().get(work_login).locked and processes('karyo-lock'),'Real manual GUI lock did not appear',seconds=60)
    wait_for(no_workers,'Manual GUI lock did not stop native workers',seconds=20)
    check_locked(False);receipt['observations'].append('manual-lock-released-channel-coordinator-survived');phase('manual-locked')
    acknowledge('manual-wrong');time.sleep(3)
    check_locked(False);receipt['observations'].append('wrong-manual-kept-tools-closed');phase('manual-wrong-confirmed')
    wait_for(lambda:not Sessions().get(work_login).locked,'Manual PAM did not unlock',seconds=60)
    final=wait_for(workers,'Manual unlock did not restore native tools',seconds=60)
    require(final['agent']==first['agent'] and final['vmusr']!=first['vmusr'] and final['bridge']!=first['bridge'],'Unlock replaced coordinator or reused dead workers')
    require(processes('karyo-shell')==shell and graph()[0]['session']['graphical']==work_graph,'Lock/unlock replaced Work session')
    receipt['workers_after_lock']=final;receipt['observations'].append('correct-manual-restored-same-native-session');phase('accepted')
finally:
    Sessions().activate(LOGIN)
    if work_graph is not None and graph():
        require(stat_start(work_graph['pid'])==work_graph['start'],'Captured Work compositor changed')
        os.kill(work_graph['pid'],15)
        wait_for(lambda:not graph(),'Captured Work runtime did not close',seconds=40)
    require(not graph() and not processes('agent') and no_workers(),'Native Work resources retained')
    if prepared:
        require(not LocalBackend.has_processes(1002),'Temporary child still running')
        if changed:
            require(digest_password(HOME)==receipt['home_password'] and digest_password(WORK)==receipt['child_password'],'Credential changed outside captured transition')
            password(accounts.password_hash(HOME),True)
        if policy_changed:
            with store.locked():
                require(store.settings(WORK)==DesktopSettings(password='separate'),'Temporary protection changed')
                store.save_settings(WORK,DesktopSettings())
        run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
    for p in (CHANNEL,RUNTIME):
        if str(p) in receipt['runtime']:require([p.stat().st_dev,p.stat().st_ino]==receipt['runtime'][str(p)],'Captured channel metadata changed')
    if receipt['runtime']:
        free_channel();CHANNEL.unlink();RUNTIME.rmdir()
    require([ACK.stat().st_dev,ACK.stat().st_ino]==receipt['ack'],'Acknowledgement changed');ACK.unlink()
    require(digest_password(HOME)==receipt['home_password'] and Sessions().get(LOGIN)==source,'Original Home changed')
    require(not run('/usr/bin/dpkg','-V','karyo-vmware-session','karyo-session-entry','karyo-profile-accounts','karyo-xclipboard') and not run('/usr/bin/dpkg','--audit'),'Package integrity changed')
    receipt['cleanup_complete']=True;save();secret=''
require(receipt['phase']=='accepted' and len(receipt['observations'])==6,'Joined GUI lock gate remains unaccepted')
print('PASS real PAM/Wayland lock and native VMware release/resume; original Home retained',flush=True)
