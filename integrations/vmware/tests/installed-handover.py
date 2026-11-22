#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real installed coordinator handover; fixed disposable native QA guests only."""
import ctypes,fcntl,hashlib,json,os,pwd,signal,socket,stat,subprocess,sys,time
from pathlib import Path
if os.getuid()!=os.geteuid() or os.getuid()!=0 or socket.gethostname() not in {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} or sys.argv[1:]:
    raise SystemExit('Use only the two disposable QA guests as root')
sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.logind import Sessions,process_session
from karyo_profiles.journal_session import stat_start
BASE='debian' if socket.gethostname()=='karyo-de-acceptance' else 'ubuntu'
HOME='karyoqa';WORK='kq-switch-work'
SHELL,LOGIN={'debian':(288593,'1631'),'ubuntu':(406670,'1808')}[BASE]
STATE=Path('/var/lib/karyo-qa/vmware-handover.json')
RUNTIME=Path('/run/karyo-vmware');CHANNEL=RUNTIME/'channel'

def require(value,message):
    if not value:raise RuntimeError(message)

def run(*args):return subprocess.check_output(args,text=True,timeout=35).strip()

def save():
    with STATE.open('w') as stream:json.dump(receipt,stream);stream.flush();os.fsync(stream.fileno())

def wait_for(function,message,seconds=45):
    until=time.monotonic()+seconds
    while True:
        result=function()
        if result:return result
        require(time.monotonic()<until,message);time.sleep(.15)

def processes(kind,uid=None):
    values=[]
    for p in Path('/proc').iterdir():
        if not p.name.isdecimal():continue
        try:
            owner=p.stat().st_uid;args=(p/'cmdline').read_bytes().split(bytes([0]))
            match=(kind=='agent' and b'/usr/bin/karyo-vmware-agent' in args or kind=='bridge' and b'/usr/bin/karyo-xclipboard' in args
                   or kind=='vmusr' and args and args[0].rsplit(b'/',1)[-1]==b'vmtoolsd' and b'vmusr' in args
                   or kind=='terminal' and args and args[0].rsplit(b'/',1)[-1]==b'karyo-terminal')
            if match and (uid is None or owner==uid):values.append(int(p.name))
        except (FileNotFoundError,ProcessLookupError):pass
    return sorted(values)

def still(value):
    try:return stat_start(value['pid'])==value['start']
    except (FileNotFoundError,ProcessLookupError):return False

def capture(pid):return {'pid':pid,'start':stat_start(pid)}

def metadata(pid,uid):
    data=Path('/proc',str(pid),'status').read_text()
    fields={line.split(':',1)[0]:line.split(':',1)[1].strip() for line in data.splitlines() if ':' in line}
    require(all(int(value)==uid for value in fields['Uid'].split()),'Unexpected worker UID')
    require(fields['NoNewPrivs']=='1' and all(int(fields[key],16)==0 for key in ('CapEff','CapPrm','CapInh','CapAmb')),'Worker retained privileges')
    require(any(os.stat(p).st_ino==CHANNEL.stat().st_ino and os.stat(p).st_dev==CHANNEL.stat().st_dev
                for p in Path('/proc',str(pid),'fd').iterdir()),'Worker lost inherited channel reservation')
    return capture(pid)

def graph():return json.loads(Path('/run/karyo/profile-host/state.json').read_bytes())['profiles']

def switch(source,target):
    p=subprocess.run(['/usr/local/libexec/karyo-qa-public-runtime','--helper',source,target],capture_output=True,text=True,timeout=65)
    require(p.returncode==0,'Public switch failed');value=json.loads(p.stdout)
    require(value['user']==target,'Wrong selected profile');return value

def user(name,*args):
    uid=pwd.getpwnam(name).pw_uid
    return ['/usr/sbin/runuser','-u',name,'--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/'+str(uid),
            'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus',*args]

def worker_set(uid):
    a=processes('agent',uid);v=processes('vmusr');b=processes('bridge',uid)
    if len(a)!=1 or len(v)!=1 or len(b)!=1:return None
    try:
        if Path('/proc',str(v[0])).stat().st_uid!=uid:return None
        value={'agent':metadata(a[0],uid),'vmusr':metadata(v[0],uid),'bridge':metadata(b[0],uid)}
        require(process_session(a[0])==process_session(v[0])==process_session(b[0]),'Worker escaped its native login scope')
        return value
    except (FileNotFoundError,ProcessLookupError):return None

def stable(uid):
    value=wait_for(lambda:worker_set(uid),'No complete ordinary native VMware worker set',seconds=60)
    time.sleep(1)
    require(worker_set(uid)==value,'Native worker set did not remain stable')
    return value

def terminate(value,number):
    require(still(value),'Captured process identity changed')
    fd=os.pidfd_open(value['pid'])
    try:
        require(still(value),'Captured process changed while opening pidfd');signal.pidfd_send_signal(fd,number)
    finally:os.close(fd)

require(not os.path.lexists(STATE),'Prior native handover receipt requires review')
require(not os.path.lexists(RUNTIME),'Prior channel runtime requires review')
require(not graph(),'Prior Work session requires review')
require(not processes('vmusr') and not processes('agent') and not processes('bridge'),'Prior native user tool requires review')
source=Sessions().get(LOGIN)
require(source.name==HOME and source.uid==1001 and source.active and not source.locked and source.vt==7,'Original Home differs')
require(Path('/proc',str(SHELL),'comm').read_text().strip()=='karyo-shell','Original Home shell differs')
installed=json.loads(Path('/var/lib/karyo-qa-vmware-install/receipt.json').read_bytes())
require(installed['phase']=='installed','Native installation not accepted')
receipt={'phase':'preparing','base':BASE,'home_session':LOGIN,'original_terminals':processes('terminal',1001),
         'home':None,'home_agent':None,'work':None,'observations':[],'runtime':{},'original_root_service':installed['root_service']}
fd=os.open(STATE,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd);save()
run('/usr/local/libexec/karyo-qa-two-sessions','prepare')
run('/usr/bin/systemd-tmpfiles','--create','/usr/lib/tmpfiles.d/karyo-vmware.conf')
for path in (RUNTIME,CHANNEL):receipt['runtime'][str(path)]=[path.stat().st_dev,path.stat().st_ino]
receipt['phase']='waiting-home';save();print('WAITING real Home GUI launch',flush=True)
work_graph=None
try:
    home_pid=wait_for(lambda:(p[0] if len(p:=processes('agent',1001))==1 else None),'Home agent did not start from GUI',seconds=90)
    receipt['home_agent']=capture(home_pid)
    terminals=set(processes('terminal',1001))-set(receipt['original_terminals'])
    require(len(terminals)==1,'Home GUI launch must own exactly one new Terminal')
    receipt['qa_terminal']=capture(terminals.pop());save()
    home=stable(1001);receipt['home']=home;save()
    require(process_session(home['agent']['pid'])==LOGIN,'Home agent was not launched in original login scope')
    receipt['observations'].append('native-home-unprivileged-reserved');receipt['phase']='home-ready';save()
    require(switch(HOME,WORK).get('starting') is True,'Work did not start cold')
    work_graph=graph()[0]['session']['graphical'];work=stable(1002);receipt['work']=work;save()
    require(not processes('vmusr',1001) and not processes('bridge',1001) and still(home['agent']),'Home did not release while its agent survived')
    active=Sessions().get(process_session(work['agent']['pid']))
    require(active.name==WORK and active.uid==1002 and active.active and not active.locked,'Wrong native Work scope')
    environment=Path('/proc',str(work['agent']['pid']),'environ').read_bytes().split(bytes([0]))
    configs=next(value for value in environment if value.startswith(b'XDG_CONFIG_DIRS='))
    require(b'/usr/share/karyo/vmware/config' in configs.split(b'=',1)[1].split(b':'),'Real Work autostart lacks its session-local mask')
    require(hashlib.sha256(Path('/etc/xdg/autostart/vmware-user.desktop').read_bytes()).hexdigest()==installed['vendor_autostart_sha256'],'Vendor autostart was modified')
    require(not run('/usr/bin/dpkg-divert','--list','/etc/xdg/autostart/vmware-user.desktop'),'Vendor file was diverted')
    receipt['observations'].append('cold-work-autostart-single-channel-mask');save()
    terminate(work['vmusr'],signal.SIGKILL)
    newer=wait_for(lambda:(value if (value:=worker_set(1002)) and value['vmusr']!=work['vmusr'] and value['bridge']!=work['bridge'] else None),
                   'Work did not recover a crashed foreground vmusr',seconds=30)
    require(newer['agent']==work['agent'],'Coordinator was replaced instead of recovering children')
    work=newer;receipt['work']=work;receipt['observations'].append('native-child-crash-recovered');save()
    require(not switch(WORK,HOME).get('starting',False),'Existing Home was recreated')
    home_now=stable(1001);require(home_now['agent']==home['agent'] and still(work['agent']),'Home/Work coordinators did not survive switching')
    require(not processes('vmusr',1002) and not processes('bridge',1002),'Background Work retained user tools')
    receipt['home']=home_now;receipt['observations'].append('existing-home-returned-and-work-released');save()
    terminate(home_now['agent'],signal.SIGKILL)
    wait_for(lambda:not processes('agent',1001) and not processes('vmusr') and not processes('bridge'),'Parent death retained a channel worker',seconds=20)
    reserve=os.open(CHANNEL,os.O_RDONLY|os.O_NOFOLLOW)
    try:fcntl.flock(reserve,fcntl.LOCK_EX|fcntl.LOCK_NB)
    finally:os.close(reserve)
    receipt['observations'].append('native-parent-death-released-channel');save()
    require(not switch(HOME,WORK).get('starting',False),'Existing Work was recreated')
    final=stable(1002);require(final['agent']==work['agent'] and graph()[0]['session']['graphical']==work_graph,'Work lease/coordinator changed')
    receipt['work']=final;receipt['observations'].append('existing-work-reused-after-home-agent-death');receipt['phase']='accepted';save()
    print(json.dumps({'accepted':receipt['observations']}),flush=True)
finally:
    if work_graph is not None and graph():
        require(stat_start(work_graph['pid'])==work_graph['start'],'Captured compositor changed')
        subprocess.run(user(WORK,'/usr/bin/systemd-run','--user','--wait','--pipe','--collect',
            '--unit=karyo-qa-vmware-logout.service','--','/usr/bin/labwc','--exit'),check=True,timeout=20)
        wait_for(lambda:not graph(),'Work runtime did not finish logout')
    if receipt['home_agent'] is not None and still(receipt['home_agent']):terminate(receipt['home_agent'],signal.SIGTERM)
    wait_for(lambda:not processes('agent') and not processes('vmusr') and not processes('bridge'),'Captured native tools remain',seconds=20)
    if receipt.get('qa_terminal') is not None and still(receipt['qa_terminal']):
        terminate(receipt['qa_terminal'],signal.SIGTERM)
        wait_for(lambda:not still(receipt['qa_terminal']),'Captured QA Terminal did not exit')
    receipt['qa_terminal_removed']=True;save()
    require(not graph(),'Retained Work needs review')
    run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
    for path in (CHANNEL,RUNTIME):
        require([path.stat().st_dev,path.stat().st_ino]==receipt['runtime'][str(path)],'Channel metadata changed')
    fd=os.open(CHANNEL,os.O_RDONLY|os.O_NOFOLLOW)
    try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB);CHANNEL.unlink();RUNTIME.rmdir()
    finally:os.close(fd)
    require(Sessions().get(LOGIN)==source and Path('/proc',str(SHELL),'comm').read_text().strip()=='karyo-shell','Original Home did not survive')
    require(run('/usr/bin/systemctl','show','open-vm-tools.service','-p','MainPID','-p','InvocationID','-p','ActiveState')==installed['root_service'].strip(),'Root tool service changed')
    require(not run('/usr/bin/dpkg','-V','karyo-vmware-session','karyo-session-entry','karyo-xclipboard','open-vm-tools','open-vm-tools-desktop') and not run('/usr/bin/dpkg','--audit'),'Package verification changed')
    receipt['cleanup_complete']=True;save()
require(receipt['phase']=='accepted','Native handover remains unaccepted')
print('PASS native VMware Home/Work handover; captured runtime removed and original Home retained',flush=True)
