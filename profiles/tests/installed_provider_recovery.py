#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual installed VPN worker failure, private reconnect and neighbor continuity."""
from pathlib import Path
import ast,json,os,signal,socket,subprocess,sys,time
assert os.getuid()==0 and socket.gethostname() in ('karyo-network-debian','karyo-network-ubuntu')
assert len(sys.argv)==2
PEER=json.loads(sys.argv[1])
sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.services import NativeProcess
root=Path(Path('/run/karyo-qa-main/root').read_text().strip());state=json.loads((root/'state.json').read_text())
def records():return json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles']
def prefix(row):
 uid,gid=row['account']['uid'],row['account']['gid']
 return ['setpriv','--reuid='+str(uid),'--regid='+str(gid),'--init-groups','--inh-caps=-all','--ambient-caps=-all','env','XDG_RUNTIME_DIR=/run/user/'+str(uid),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus']
def code_for(row,code,**values):
 values.update(NETWORK=row['network']['namespace'],RESOLVER=row['dns']['resolver']['runtime'][0],PEER=PEER)
 code='\n'.join(k+'='+repr(v) for k,v in values.items())+'\n'+code;ast.parse(code);return code

def probe(row):
 p=subprocess.run([*prefix(row),'systemd-run','--user','--quiet','--wait','--pipe','--collect','--unit=karyo-qa-provider-probe','/usr/bin/python3','-I','-c',code_for(row,CLIENT_CODE)],capture_output=True,text=True,timeout=30)
 assert p.returncode==0,p.stderr[-2200:]
 return json.loads(p.stdout)
def private_key(row):return json.loads((Path('/var/lib/karyo/profile-vpn')/str(row['account']['uid'])/'netbird/config.json').read_text())['PrivateKey']
CLIENT_CODE = r'''
import hashlib,json,os,socket,subprocess,urllib.request
from pathlib import Path
def identity(p):
 n=Path(p).stat();return [n.st_dev,n.st_ino]
assert identity('/proc/self/ns/net')==NETWORK and identity('/run/systemd/resolve')==RESOLVER
value=json.loads(subprocess.check_output(['netbird','status','--json'],text=True,timeout=10));assert value['management']['connected'] and value['signal']['connected']
assert socket.getaddrinfo('example.com',443,socket.AF_INET)
with urllib.request.urlopen('https://example.com',timeout=8) as response:assert response.status==200
assert {v[4][0] for v in socket.getaddrinfo(PEER['fqdn'],PEER['port'],socket.AF_INET)}=={PEER['ip']}
data=os.urandom(512);expected=PEER['host'].encode()+b':'+hashlib.sha256(data).digest()
with socket.create_connection((PEER['ip'],PEER['port']),timeout=6) as stream:
 stream.sendall(data);answer=b''
 while len(answer)<len(expected):
  block=stream.recv(1024);assert block;answer+=block
 assert answer==expected
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
 udp.settimeout(6);udp.sendto(data,(PEER['ip'],PEER['port']));assert udp.recv(1024)==expected
assert json.loads(subprocess.check_output(['ip','-j','-4','route','get',PEER['ip']],text=True))[0]['dev']=='wt0'
print(json.dumps({'connected':True,'saved_state_reconnect':True,'managed_dns_tcp_udp':True,'external_dns_https':True}))
'''

CONTINUITY_CODE = r'''
import hashlib,json,os,socket,subprocess,time
from pathlib import Path
path=Path(PROGRESS);temporary=path.with_suffix('.tmp');value={'count':0,'errors':0,'one_tcp_connection':True}
def publish():
 temporary.write_text(json.dumps(value));os.replace(temporary,path)
def identity(p):
 n=Path(p).stat();return [n.st_dev,n.st_ino]
publish()
try:
 assert identity('/proc/self/ns/net')==NETWORK and identity('/run/systemd/resolve')==RESOLVER
 with socket.create_connection((PEER['ip'],PEER['port']),timeout=6) as stream,socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
  udp.settimeout(6)
  route=json.loads(subprocess.check_output(['/usr/sbin/ip','-j','-4','route','get',PEER['ip']],text=True));assert route[0]['dev']=='wt0'
  end=time.monotonic()+180
  while time.monotonic()<end:
   values={v[4][0] for v in socket.getaddrinfo(PEER['fqdn'],PEER['port'],socket.AF_INET)};assert values=={PEER['ip']}
   data=os.urandom(512);expected=PEER['host'].encode()+b':'+hashlib.sha256(data).digest();stream.sendall(data);answer=b''
   while len(answer)<len(expected):
    block=stream.recv(1024);assert block;answer+=block
   assert answer==expected
   udp.sendto(data,(PEER['ip'],PEER['port']));assert udp.recv(1024)==expected
   value['count']+=1;publish();time.sleep(.25)
except BaseException as error:
 value.update(errors=1,error=type(error).__name__);publish();raise
'''

proof={'roles':{}}
for role in ('home','work'):
 rows=records();assert len(rows)==2
 selected=next(r for r in rows if r['account']['name']==state['name' if role=='home' else 'work_name']);neighbor=next(r for r in rows if r is not selected)
 probe(selected);probe(neighbor);oldkey=private_key(selected)
 progress=Path('/run/user')/str(neighbor['account']['uid'])/('karyo-qa-provider-failure-'+role+'.json');assert not progress.exists()
 unit='karyo-qa-provider-continuity.service'
 assert subprocess.check_output([*prefix(neighbor),'systemctl','--user','show',unit,'-p','LoadState','--value'],text=True).strip()=='not-found'
 subprocess.run([*prefix(neighbor),'systemd-run','--user','--quiet','--collect','--unit='+unit,'/usr/bin/python3','-I','-c',code_for(neighbor,CONTINUITY_CODE,PROGRESS=str(progress))],check=True,timeout=10)
 inv=subprocess.check_output([*prefix(neighbor),'systemctl','--user','show',unit,'-p','InvocationID','--value'],text=True).strip();assert len(inv)==32
 (root/('provider-failure-'+role+'-unit.json')).write_text(json.dumps({'unit':unit,'invocation':inv,'uid':neighbor['account']['uid'],'progress':str(progress)},indent=2)+'\n')
 def flow():
  value=json.loads(progress.read_text());assert value['errors']==0,value
  assert next(r for r in records() if r['token']==neighbor['token'])==neighbor
  return value['count']
 try:
  deadline=time.monotonic()+10
  while not progress.exists() or flow()<3:
   assert time.monotonic()<deadline;time.sleep(.1)
  initial=flow();native=selected['provider']['service']['unit'];cg='/system.slice/karyo-netbird-'+selected['token']+'.service'
  descriptor=os.pidfd_open(native['pid'])
  try:
   assert NativeProcess.read(native['pid'],tuple(native['exe']),0,cg,tuple(native['namespace']))==native['start']
   signal.pidfd_send_signal(descriptor,signal.SIGKILL)
  finally:os.close(descriptor)
  deadline=time.monotonic()+50
  while True:
   current=next(r for r in records() if r['token']==selected['token']);value=current['provider'];flow()
   if value['pending'] is None and value['service'] is not None and value['service']['unit']['invocation']!=native['invocation']:break
   assert time.monotonic()<deadline,'The installed host did not replace the failed VPN worker';time.sleep(.2)
  assert {k:v for k,v in current.items() if k!='provider'}=={k:v for k,v in selected.items() if k!='provider'}
  assert current['provider']['state']==selected['provider']['state'] and private_key(current)==oldkey
  deadline=time.monotonic()+30
  while True:
   try:accepted=probe(current);break
   except AssertionError:
    flow();assert time.monotonic()<deadline;time.sleep(.5)
  after=flow();assert after>initial
  proof['roles'][role]=dict(accepted,namespace_resolver_and_graphical_owner_retained=True,provider_state_and_key_retained=True,
   new_worker_acknowledged=True,neighbor_record_unchanged=True,one_existing_neighbor_tcp_dns_udp_survived=True,payloads_before=initial,payloads_after=after)
 finally:
  current_inv=subprocess.check_output([*prefix(neighbor),'systemctl','--user','show',unit,'-p','InvocationID','--value'],text=True).strip()
  if current_inv==inv:subprocess.run([*prefix(neighbor),'systemctl','--user','stop',unit],check=True,timeout=10)
before=records();subprocess.run(['systemctl','restart','karyo-profile-host.service'],check=True,timeout=20)
assert records()==before
for row in records():probe(row)
assert json.loads(Path('/run/karyo/profile-host/state.json').read_text())['schema']==20
proof.update(controller_reopen_preserved_recovered_workers=True,schema=20)
(root/'provider-recovery-result.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
