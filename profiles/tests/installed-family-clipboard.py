#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real fixed-host Home/Work text/PNG sharing and outsider denial acceptance."""
import hashlib,json,os,socket,stat,struct,subprocess,sys,time,uuid,zlib
from pathlib import Path
if os.getuid()!=os.geteuid() or os.getuid()!=0 or socket.gethostname() not in {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} or sys.argv[1:]:
    raise SystemExit('Use only fixed disposable acceptance hosts as root')
sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.journal_session import stat_start
from karyo_profiles.logind import Sessions

STATE=Path('/var/lib/karyo-qa/family-clipboard.json')
MAIN='karyoqa';CHILD='kq-switch-work';MAIN_UNIT='karyo-qa-family-clipboard.service'
BROKER='karyo-profile-clipboard.service';SOCKET='karyo-profile-clipboard.socket'
OWNERS=[]
TEXT='text/plain;charset=utf-8';PNG='image/png';HINT='x-kde-passwordManagerHint'


def require(value,message):
    if not value:raise RuntimeError(message)


def run(*args):return subprocess.check_output(args,text=True,timeout=35).strip()


def wait_for(predicate,message,seconds=45):
    deadline=time.monotonic()+seconds
    while not predicate():
        require(time.monotonic()<deadline,message);time.sleep(.15)


def save():
    with STATE.open('w') as stream:json.dump(receipt,stream);stream.flush();os.fsync(stream.fileno())


def user(name,*args):
    uid=LocalAccounts().account(name).uid
    return ['/usr/sbin/runuser','-u',name,'--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/'+str(uid),
            'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus',*args]


def managed(name,*args,data=None):
    result=subprocess.run(user(name,'/usr/bin/systemd-run','--user','--wait','--pipe','--collect',
        '--unit=karyo-qa-clipboard-op-'+uuid.uuid4().hex+'.service','--',*args),input=data,
        stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=15)
    require(result.returncode==0,'Ordinary clipboard operation failed: '+str(args[0]))
    return result.stdout


def copy(name,mime,data):
    # The owner must outlive the copy request: wl-copy's default fork would be
    # killed when a one-shot transient service exits and clears its selection.
    unit_name='karyo-qa-clipboard-source-'+uuid.uuid4().hex+'.service'
    child=subprocess.Popen(user(name,'/usr/bin/systemd-run','--user','--pipe','--collect','--unit='+unit_name,
        '--property=Type=exec','--','/usr/bin/wl-copy','--foreground','--type',mime),
        stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    OWNERS.append((name,unit_name,child))
    child.stdin.write(data);child.stdin.close()
    wait_for(lambda:matches(name,mime,data),'QA foreground selection owner did not publish',seconds=12)



def paste(name,mime):
    try:return managed(name,'/usr/bin/wl-paste','--no-newline','--type',mime)
    except RuntimeError:return None


def matches(name,mime,data):return paste(name,mime)==data


def unit(name):
    return dict(line.split('=',1) for line in run('/usr/bin/systemctl','show',name,'-p','ActiveState','-p','MainPID','-p','InvocationID').splitlines())


def journal():return json.loads(Path('/run/karyo/profile-host/state.json').read_bytes())


def switch(source,target):
    result=subprocess.run(['/usr/local/libexec/karyo-qa-public-runtime','--helper',source,target],capture_output=True,text=True,timeout=65)
    require(result.returncode==0,'Public switch failed')
    value=json.loads(result.stdout);require(value['user']==target,'Wrong selected desktop');return value


def processes(uid):
    found=[]
    for path in Path('/proc').iterdir():
        if not path.name.isdecimal():continue
        try:
            args=(path/'cmdline').read_bytes().split(bytes([0]))
            if (path.stat().st_uid==uid and args and Path(os.fsdecode(args[0])).name.startswith('python3')
                    and b'/usr/bin/karyo-profile-clipboard' in args and b'--notify' not in args):
                found.append(int(path.name))
        except (FileNotFoundError,ProcessLookupError):pass
    return found


def png():
    def chunk(kind,data):return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data))
    return bytes([137,80,78,71,13,10,26,10])+chunk(b'IHDR',struct.pack('!IIBBBBB',2,2,8,6,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes([35,150,85,255])*2)*2))+chunk(b'IEND',b'')


require(not os.path.lexists(STATE),'Prior clipboard receipt requires review')
require(not journal()['profiles'],'Prior profile requires review')
require(unit(SOCKET)['ActiveState']==unit(BROKER)['ActiveState']=='inactive','Clipboard must start inactive')
require(not os.path.lexists('/run/karyo/clipboard.sock'),'Prior clipboard socket')
source=next(row for row in Sessions().all() if row.name==MAIN and row.uid==1001 and row.active)
require(not processes(1001),'Existing Home bridge requires review')
manager=dict(line.split('=',1) for line in run(*user(MAIN,'/usr/bin/systemctl','--user','show-environment')).splitlines() if '=' in line)
require(manager.get('WAYLAND_DISPLAY'),'Home native manager lacks Wayland')
lock=Path('/run/user/1001')/('karyo-clipboard-'+hashlib.sha256(manager['WAYLAND_DISPLAY'].encode()).hexdigest()+'.lock')
require(not os.path.lexists(lock),'Prior Home bridge lock requires review')
receipt={'phase':'preparing','home_session':source.id,'main_lock':str(lock),'broker':None,'home_unit':None,'observations':[]}
fd=os.open(STATE,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd);save()
run('/usr/local/libexec/karyo-qa-two-sessions','prepare')
graph=None
try:
    run('/usr/bin/systemctl','start',SOCKET)
    run(*user(MAIN,'/usr/bin/systemd-run','--user','--collect','--unit='+MAIN_UNIT,'--property=Type=exec',
              '--property=KillMode=control-group','--','/usr/bin/karyo-profile-clipboard'))
    wait_for(lambda:len(processes(1001))==1,'Ordinary Home bridge did not remain running')
    receipt['home_unit']=dict(line.split('=',1) for line in run(*user(MAIN,'/usr/bin/systemctl','--user','show',MAIN_UNIT,
        '-p','InvocationID','-p','MainPID','-p','ActiveState')).splitlines());receipt['home_start']=stat_start(int(receipt['home_unit']['MainPID']));save()
    first=('Home to Work \u2192 '+uuid.uuid4().hex+'\nПривет').encode()
    copy(MAIN,TEXT,first)
    require(switch(MAIN,CHILD).get('starting') is True,'Work was not started cold')
    graph=journal()['profiles'][0]['session']['graphical']
    wait_for(lambda:len(processes(1002))==1,'Fresh Work autostart did not launch one bridge')
    receipt['phase']='first-transfer';save()
    try:wait_for(lambda:matches(CHILD,TEXT,first),'Home text did not reach Work')
    except RuntimeError:
        for who in (MAIN,CHILD):
            actual=paste(who,TEXT)
            print(json.dumps({'clipboard_probe':who,'text_bytes':None if actual is None else len(actual),
                'expected_match':actual==first,'bridge_processes':processes(LocalAccounts().account(who).uid)}),flush=True)
        raise
    second=('Work to Home '+uuid.uuid4().hex).encode();copy(CHILD,TEXT,second)
    wait_for(lambda:matches(MAIN,TEXT,second),'Work text did not reach Home')
    picture=png();copy(MAIN,PNG,picture)
    wait_for(lambda:matches(CHILD,PNG,picture),'PNG changed or did not reach Work')
    copy(CHILD,TEXT,second);wait_for(lambda:matches(MAIN,TEXT,second),'Work text did not replace PNG')
    hint=b'Karyo QA sensitive marker';copy(MAIN,HINT,hint)
    time.sleep(1)
    require(matches(CHILD,TEXT,second),'Sensitive hint was shared')
    third=('Work remote while Home sensitive '+uuid.uuid4().hex).encode();copy(CHILD,TEXT,third)
    time.sleep(1)
    require(matches(MAIN,HINT,hint),'Remote content overwrote a sensitive local selection')
    final=('Final family marker '+uuid.uuid4().hex).encode();copy(MAIN,TEXT,final)
    wait_for(lambda:matches(CHILD,TEXT,final),'Normal Home copy did not resume sharing')
    probe="import sys,socket;sys.path.insert(0,'/usr/libexec/karyo/profiles');from karyo_profiles.clipboard import SOCKET,ZERO,send,receive,request,parse_response;s=socket.socket(socket.AF_UNIX);s.connect(SOCKET);send(s,request(ord('G')));assert parse_response(receive(s))[0]==b'E';print('outsider denied')"
    denied=subprocess.run(['/usr/sbin/runuser','-u','qaadmin','--','/usr/bin/env','USER=karyoqa','LOGNAME=kq-switch-work',
        '/usr/bin/python3','-I','-c',probe],capture_output=True,text=True,timeout=10)
    require(denied.returncode==0 and denied.stdout.strip()=='outsider denied','Spoofed outsider reached family data')
    before=unit(BROKER);require(before['ActiveState']=='active','Broker inactive')
    status=Path('/proc',before['MainPID'],'status').read_text().splitlines()
    fields={line.split(':',1)[0]:line.split(':',1)[1].strip() for line in status if ':' in line}
    require(int(fields['Uid'].split()[0]) not in (0,1000,1001,1002),'Broker did not use DynamicUser')
    require(fields['NoNewPrivs']=='1' and all(int(fields[name],16)==0 for name in ('CapEff','CapPrm','CapBnd','CapAmb')),'Broker retained privileges')
    require(os.stat('/proc/'+before['MainPID']+'/ns/net').st_ino!=os.stat('/proc/1/ns/net').st_ino,'Broker shared host network')
    run('/usr/bin/systemctl','restart',BROKER)
    copy(MAIN,TEXT,final+b' after broker restart')
    wait_for(lambda:matches(CHILD,TEXT,final+b' after broker restart'),'Sharing did not recover after volatile broker restart')
    require(unit(BROKER)['InvocationID']!=before['InvocationID'],'Broker was not restarted')
    require(not switch(CHILD,MAIN).get('starting',False) and not switch(MAIN,CHILD).get('starting',False),'Existing sessions recreated')
    require(journal()['profiles'][0]['session']['graphical']==graph,'Graphical lease changed')
    receipt.update(phase='accepted',observations=['text-both-directions','binary-png-exact','sensitive-local-only','outsider-spoof-denied',
        'broker-no-caps-private-net','broker-restart-recovered','running-switches-reused']);save()
    print(json.dumps({'accepted':receipt['observations']}),flush=True)
except BaseException:
    for name,uid in ((MAIN,1001),(CHILD,1002)):
        found=[]
        for path in Path('/proc').iterdir():
            if not path.name.isdecimal():continue
            try:
                args=(path/'cmdline').read_bytes().split(bytes([0]))
                if path.stat().st_uid==uid and any(b'clipboard' in arg or b'wl-paste' in arg for arg in args):
                    found.append({'pid':path.name,'arguments':[os.fsdecode(arg) for arg in args if arg]})
            except (FileNotFoundError,ProcessLookupError):pass
        print(json.dumps({'bridge_diagnostic':name,'processes':found}),flush=True)
    log=Path('/home/kq-switch-work/.local/state/karyo/session.log')
    if log.exists():print(json.dumps({'work_client_errors':[line for line in log.read_text(errors='replace').splitlines() if 'karyo-profile-clipboard:' in line][-8:]}),flush=True)
    print('installed autostart includes clipboard:', 'karyo-profile-clipboard' in Path('/usr/share/karyo/session/labwc/autostart').read_text(),flush=True)
    raise
finally:
    if graph is not None and journal()['profiles']:
        require(stat_start(graph['pid'])==graph['start'],'Captured compositor changed')
        managed(CHILD,'/usr/bin/labwc','--exit')
        wait_for(lambda:not journal()['profiles'],'Work supervisor did not finish cleanup')
    require(not journal()['profiles'],'Retained Work needs review')
    if receipt['home_unit'] is not None:
        current=dict(line.split('=',1) for line in run(*user(MAIN,'/usr/bin/systemctl','--user','show',MAIN_UNIT,
            '-p','InvocationID','-p','MainPID','-p','ActiveState')).splitlines())
        require(current==receipt['home_unit'] and stat_start(int(current['MainPID']))==receipt['home_start'],'Home bridge identity changed')
        run(*user(MAIN,'/usr/bin/systemctl','--user','stop',MAIN_UNIT))
    run('/usr/bin/systemctl','stop',BROKER,SOCKET)
    require(not os.path.lexists('/run/karyo/clipboard.sock'),'Clipboard socket retained')
    managed(MAIN,'/usr/bin/wl-copy','--clear')
    for name,unit_name,child in OWNERS:
        if name==MAIN:
            state=run(*user(name,'/usr/bin/systemctl','--user','show',unit_name,'-p','ActiveState','--value'))
            if state=='active':run(*user(name,'/usr/bin/systemctl','--user','stop',unit_name))
        try:child.wait(timeout=3)
        except subprocess.TimeoutExpired:child.terminate();child.wait(timeout=3)

    if lock.exists():
        info=lock.lstat();require(stat.S_ISREG(info.st_mode) and info.st_uid==1001 and info.st_nlink==1 and info.st_size==0 and stat.S_IMODE(info.st_mode)==0o600,'Home lock changed')
        import fcntl
        fd=os.open(lock,os.O_RDONLY|os.O_NOFOLLOW|os.O_CLOEXEC)
        try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB);lock.unlink()
        finally:os.close(fd)
    run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
    require(Sessions().get(source.id)==source and not processes(1001),'Original Home/session not retained')
    receipt['phase']='complete';save()
require(not run('/usr/bin/dpkg','-V','karyo-profile-clipboard','karyo-profile-accounts','karyo-session-entry') and not run('/usr/bin/dpkg','--audit'),'Package integrity changed')
print('PASS native family clipboard; original Home/accounts retained and captured clipboard fixtures removed',flush=True)
