#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
import json,os,re,socket,stat,subprocess,sys,time,uuid,fcntl
from pathlib import Path
assert os.getuid()==0 and socket.gethostname() in {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} and not sys.argv[1:]
sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.logind import Sessions
from karyo_profiles.journal_session import stat_start
BASE='debian' if socket.gethostname()=='karyo-de-acceptance' else 'ubuntu'
SHELL,LOGIN={'debian':(288593,'1631'),'ubuntu':(406670,'1808')}[BASE]
STATE=Path('/var/lib/karyo-qa/vmware-display.json');RUNTIME=Path('/run/karyo-vmware');CHANNEL=RUNTIME/'channel'

def require(value,message):
 if not value:raise RuntimeError(message)

def run(*args):return subprocess.check_output(args,text=True,timeout=25).strip()

def wait_for(fn,message):
 end=time.monotonic()+60
 while not fn():require(time.monotonic()<end,message);time.sleep(.12)

def graph():return json.loads(Path('/run/karyo/profile-host/state.json').read_bytes())['profiles']

def managed(uid,*args):
 return run('/usr/sbin/runuser','-u','karyoqa' if uid==1001 else 'kq-switch-work','--','/usr/bin/env',
  'XDG_RUNTIME_DIR=/run/user/'+str(uid),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus',
  '/usr/bin/systemd-run','--user','--wait','--pipe','--quiet','--collect','--unit=karyo-qa-display-'+uuid.uuid4().hex+'.service','--',*args)

def preferred():
 rows={}
 for p in Path('/sys/class/drm').glob('card*-*'):
  if (p/'status').read_text().strip()=='connected':rows[p.name.split('-',1)[1]]=(p/'modes').read_text().splitlines()[0]
 return rows

def current(uid):
 name=None;result={}
 for line in managed(uid,'/usr/bin/wlr-randr').splitlines():
  if line and not line[0].isspace():name=line.split()[0]
  match=re.search(r'^\s+(\d+x\d+) px.*\bcurrent\b',line)
  if name and match:result[name]=match[1]
 return result

def followers():
 return [int(p.name) for p in Path('/proc').iterdir() if p.name.isdecimal() and b'/usr/libexec/karyo/vmware/display-follower' in read_cmdline(p).split(bytes([0]))]

def metadata(pid):
 p=Path('/proc',str(pid));fields=dict(line.split(':',1) for line in (p/'status').read_text().splitlines() if ':' in line)
 require(all(int(x)==1002 for x in fields['Uid'].split()),'Follower UID changed')
 require(fields['NoNewPrivs'].strip()=='1' and all(int(fields[k],16)==0 for k in ('CapEff','CapPrm','CapInh','CapAmb')),'Follower retained capabilities')
 require(any(os.stat(fd).st_ino==CHANNEL.stat().st_ino and os.stat(fd).st_dev==CHANNEL.stat().st_dev for fd in (p/'fd').iterdir()),'Follower lost reservation')
 from karyo_profiles.logind import process_session
 require(process_session(pid)==captured['login']['id'],'Follower escaped native session')
 return {'pid':pid,'start':stat_start(pid),'uid':1002,'session':process_session(pid),'capabilities':'zero','no_new_privs':True,'channel_fd':True}

def read_cmdline(p):
 try:return (p/'cmdline').read_bytes()
 except (FileNotFoundError,ProcessLookupError,PermissionError):return b''

def save():STATE.write_text(json.dumps(receipt));os.chmod(STATE,0o600)

require(not STATE.exists() and not RUNTIME.exists() and not graph(),'Prior display fixture requires review')
original=preferred();require(original=={'Virtual-1':'1280x800'},'Unexpected fixed QA topology')
source=Sessions().get(LOGIN);require(source.active and not source.locked and source.uid==1001,'Original Home differs')
receipt={'phase':'preparing','preferred_before':original,'home_current_before':current(1001)}
fd=os.open(STATE,os.O_CREAT|os.O_EXCL|os.O_WRONLY|os.O_NOFOLLOW,0o600);os.close(fd);save()
prepared=changed=False;captured=None
try:
 run('/usr/local/libexec/karyo-qa-two-sessions','prepare');prepared=True
 run('/usr/bin/systemd-tmpfiles','--create','/usr/lib/tmpfiles.d/karyo-vmware.conf')
 runtime={str(p):[p.stat().st_dev,p.stat().st_ino] for p in (RUNTIME,CHANNEL)}
 result=subprocess.run(['/usr/local/libexec/karyo-qa-public-runtime','--helper','karyoqa','kq-switch-work'],capture_output=True,text=True,timeout=65)
 require(result.returncode==0 and json.loads(result.stdout)['starting'],'Work did not start cold')
 wait_for(lambda:bool(graph()) and bool(graph()[0]['session'].get('graphical')),'Work lease missing')
 captured=graph()[0]['session']['graphical']
 wait_for(lambda:len([p for p in Path('/proc').iterdir() if p.name.isdecimal() and (p/'cmdline').exists() and b'/usr/bin/karyo-vmware-agent\0' in read_cmdline(p)])==1,'Work agent missing')
 wait_for(lambda:len(followers())==1,'Installed foreground display helper missing')
 receipt['initial_helper']=metadata(followers()[0]);save();time.sleep(4)
 receipt['private_modes']=managed(1002,'/usr/bin/python3','-I','-c',"import sys;sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/vmware');from karyo_vmware.display import preferred_modes;print(preferred_modes())");receipt['raw_current']=managed(1002,'/usr/bin/wlr-randr');save()
 receipt['work_current_before']=current(1002)
 require(receipt['work_current_before']==original,'Initial Work did not use kernel preferred size')
 changed=True;managed(1002,'/usr/bin/wlr-randr','--output','Virtual-1','--custom-mode','1152x720')
 wait_for(lambda:current(1002).get('Virtual-1')=='1152x720','Manual Work drift missing')
 time.sleep(4)
 receipt['manual_mode_retained']=current(1002)
 require(receipt['manual_mode_retained']=={'Virtual-1':'1152x720'} and preferred()==original,'Unchanged preferred mode overwrote manual selection')
 Sessions().activate(LOGIN)
 wait_for(lambda:not followers(),'Background follower survived')
 receipt['background_helper_absent']=True;save()
 Sessions().activate(captured['login']['id'])
 wait_for(lambda:len(followers())==1,'Reactivated helper missing')
 receipt['reactivation_modes']=managed(1002,'/usr/bin/python3','-I','-c',"import sys;sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/vmware');from karyo_vmware.display import preferred_modes;print(preferred_modes())");receipt['reactivation_raw']=managed(1002,'/usr/bin/wlr-randr');receipt['reactivation_helper']=metadata(followers()[0]);save()
 wait_for(lambda:current(1002)==original,'Reactivated Work did not adopt preferred mode')
 receipt['reactivated_helper']=metadata(followers()[0])
 require(receipt['reactivated_helper']['pid']!=receipt['initial_helper']['pid'],'Old background helper reused')
 receipt.update(phase='accepted',preferred_requested=preferred(),work_current_after=current(1002),
   private_kernel_mode=managed(1002,'/usr/bin/head','-1','/sys/class/drm/card0-Virtual-1/modes'))
 require(receipt['private_kernel_mode']==original['Virtual-1'],'Private sysfs mode differs');save()
 print(json.dumps(receipt),flush=True)
finally:
 if changed:
  if captured is not None and graph() and current(1002).get('Virtual-1')!='1280x800':managed(1002,'/usr/bin/wlr-randr','--output','Virtual-1','--custom-mode','1280x800')
 Sessions().activate(LOGIN)
 if captured is not None and graph():
  require(stat_start(captured['pid'])==captured['start'],'Captured Work changed');os.kill(captured['pid'],15)
  wait_for(lambda:not graph(),'Work cleanup incomplete')
 if prepared:
  run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
  for p in (CHANNEL,RUNTIME):require([p.stat().st_dev,p.stat().st_ino]==runtime[str(p)],'Owned display runtime changed')
  fd=os.open(CHANNEL,os.O_RDONLY|os.O_NOFOLLOW)
  try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB);CHANNEL.unlink();RUNTIME.rmdir()
  finally:os.close(fd)
 require(Sessions().get(LOGIN)==source and preferred()==original and current(1001)==receipt['home_current_before'],'Original Home/topology did not survive')
 receipt['cleanup_complete']=True;save()
 print('PASS original Home/topology/runtime restored',flush=True)
