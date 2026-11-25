#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stock WireGuard or OpenVPN beside NetBird in either desktop role.

Run only on the two named QA hosts after the installed LightDM/family gate has
created root-owned /run/karyo-qa-main/root and state.json with name/work_name.
Both desktops must be live with their native user managers. No user VPN state
or credentials are imported. Temporarily disconnect only the selected desktop NetBird, then restore
it after retiring only that fixture VPN. Keep generated TLS evidence on the VM.
"""
from pathlib import Path
from types import SimpleNamespace
import argparse,ast,hashlib,json,os,socket,subprocess,sys,time

assert os.getuid()==0 and socket.gethostname() in ('karyo-network-debian','karyo-network-ubuntu')
parser=argparse.ArgumentParser(allow_abbrev=False)
parser.add_argument('kind',choices=('wireguard','openvpn'))
parser.add_argument('peer')
parser.add_argument('--nm-role',choices=('home','work'),default='home')
args=parser.parse_args();KIND=args.kind;PEER=json.loads(args.peer)

sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.networks import NetworkSpec,NetworkCommand
from native_main_network import host_snapshot
import native_wireguard,native_openvpn

root=Path(Path('/run/karyo-qa-main/root').read_text().strip())
state=json.loads((root/'state.json').read_text())
def records():return json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles']
rows=records();assert len(rows)==2
before=host_snapshot(NetworkCommand())
host_nm=subprocess.check_output(['systemctl','show','--property=MainPID','--value','NetworkManager.service'],text=True).strip()
def host_config():
    return {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for d in ('/etc/NetworkManager','/etc/netplan')
                                    for p in Path(d).rglob('*') if p.is_file() and not p.is_symlink()}
before_config=host_config()
contexts={}
for role,name in (('home',state['name']),('work',state['work_name'])):
    record=next(r for r in rows if r['account']['name']==name)
    contexts[role]=SimpleNamespace(record=record,network=SimpleNamespace(spec=NetworkSpec(record['account']['uid'],record['token'])),
                                                                                                                            state=SimpleNamespace(account=SimpleNamespace(**record['account'])))
contexts['home'].dns=contexts['home'];contexts['work'].dns=contexts['work']

def user_code(scope,code,*args):
    ast.parse(code)
    a=scope.state.account
    result=subprocess.run(['setpriv','--reuid='+str(a.uid),'--regid='+str(a.gid),'--init-groups','--inh-caps=-all','--ambient-caps=-all',
        'env','XDG_RUNTIME_DIR=/run/user/'+str(a.uid),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(a.uid)+'/bus',
        'systemd-run','--user','--quiet','--wait','--pipe','--collect','--unit=karyo-installed-vpn-'+str(a.uid),
        '/usr/bin/python3','-I','-c',code,*args],capture_output=True,text=True,timeout=25)
    assert result.returncode==0,result.stderr[-3000:]
    return result.stdout

imported={role:[] for role in contexts}
def ordinary_cli(scope,*args):
    code="import subprocess,sys\nr=subprocess.run(['nmcli','--wait','10',*sys.argv[1:]],capture_output=True,text=True,timeout=16)\nassert r.returncode==0,r.stderr\nprint(r.stdout,end='')\n"
    result=user_code(scope,code,*args)
    if args[:2]==('connection','import'):
        name='wgqa' if args[3]=='wireguard' else 'openvpn_fixture'
        connection=user_code(scope,code,'--get-values','connection.uuid','connection','show',name).strip()
        import uuid
        assert str(uuid.UUID(connection))==connection
        imported[next(role for role,c in contexts.items() if c is scope)].append(connection)
    return result

def client(scope,endpoint):
    code='''import json,os,socket,sys
from pathlib import Path
domain,answer,host,tcp,udp,label,expected,resolver=sys.argv[1:]
def identity(path):
 n=Path(path).stat();return [n.st_dev,n.st_ino]
assert identity('/proc/self/ns/net')==json.loads(expected)
assert identity('/run/systemd/resolve')==json.loads(resolver)
values={v[4][0] for v in socket.getaddrinfo(domain,None,socket.AF_INET)}
assert values=={answer},values
with socket.create_connection((host,int(tcp)),timeout=4) as stream:assert stream.recv(64).decode()==label
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as stream:
 stream.settimeout(4);stream.sendto(b'actual installed family',(host,int(udp)));assert stream.recv(64).decode()==label
neighbor=('work' if label=='home' else 'home')+'.context.test'
try:socket.getaddrinfo(neighbor,None,socket.AF_INET)
except socket.gaierror:pass
else:raise RuntimeError('Neighbor DNS view leaked')
print(json.dumps({'dns':True,'tcp':True,'udp':True,'separate_dns_view':True,'actual_user_manager_context':True}))
'''
    return json.loads(user_code(scope,code,endpoint.domain,endpoint.answer,endpoint.tcp.getsockname()[0],
                                                                        str(endpoint.tcp.getsockname()[1]),str(endpoint.udp.getsockname()[1]),endpoint.label,
                                                                        json.dumps(scope.record['network']['namespace']),json.dumps(scope.record['dns']['resolver']['runtime'][0])))


CLIENT_CODE=r'''

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
print(json.dumps({'connected':True,'managed_dns_tcp_udp':True,'external_dns_https':True}))
'''

CONTINUITY_CODE=r'''

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


native_wireguard.client=client;native_openvpn.client=client
selected=contexts[args.nm_role];neighbor=contexts['work' if args.nm_role=='home' else 'home'];assert selected.record['provider']['kind']==neighbor.record['provider']['kind']=='netbird'
neighbor_before=neighbor.record;selected_before=selected.record
unit='karyo-qa-mixed-netbird-continuity-'+args.nm_role+'-'+KIND+'.service'
result_file=root/('mixed-netbird-'+args.nm_role+'-'+KIND+'.json');assert not result_file.exists()
progress=Path('/run/user')/str(neighbor.state.account.uid)/('karyo-qa-mixed-netbird-'+args.nm_role+'-'+KIND+'.json');assert not progress.exists()
def program(scope,code,**values):
 values.update(NETWORK=scope.record['network']['namespace'],RESOLVER=scope.record['dns']['resolver']['runtime'][0],PEER=PEER)
 return '\n'.join(k+'='+repr(v) for k,v in values.items())+'\n'+code

def nb(scope,*args):return user_code(scope,"import subprocess,sys\np=subprocess.run(['netbird',*sys.argv[1:]],capture_output=True,text=True,timeout=18)\nassert p.returncode==0,p.stderr\nprint(p.stdout,end='')\n",*args)
def nb_probe(scope):return json.loads(user_code(scope,program(scope,CLIENT_CODE)))
def flow():
 v=json.loads(progress.read_text());assert v['errors']==0,v
 assert next(row for row in records() if row['token']==neighbor_before['token'])==neighbor_before
 return v['count']
a=neighbor.state.account
user=['setpriv','--reuid='+str(a.uid),'--regid='+str(a.gid),'--init-groups','--inh-caps=-all','--ambient-caps=-all','env','XDG_RUNTIME_DIR=/run/user/'+str(a.uid),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(a.uid)+'/bus']
assert subprocess.check_output([*user,'systemctl','--user','show',unit,'-p','LoadState','--value'],text=True).strip()=='not-found'
nb_probe(selected);nb_probe(neighbor)
subprocess.run([*user,'systemd-run','--user','--quiet','--collect','--unit='+unit,'/usr/bin/python3','-I','-c',program(neighbor,CONTINUITY_CODE,PROGRESS=str(progress))],check=True,timeout=10)
inv=subprocess.check_output([*user,'systemctl','--user','show',unit,'-p','InvocationID','--value'],text=True).strip();assert len(inv)==32
(root/('mixed-netbird-'+args.nm_role+'-'+KIND+'-continuity.json')).write_text(json.dumps({'unit':unit,'invocation':inv,'uid':a.uid,'progress':str(progress)},indent=2)+'\n')
peer=None;initial=None;initial_count=None;error=None;proof={'kind':KIND,'nm_role':args.nm_role,'providers':{'home':KIND if args.nm_role=='home' else 'netbird','work':KIND if args.nm_role=='work' else 'netbird'},'stages':{}}
try:
 deadline=time.monotonic()+10
 while not progress.exists() or flow()<3:
  assert time.monotonic()<deadline;time.sleep(.1)
 initial_count=flow();nb(selected,'down')
 deadline=time.monotonic()+10
 while 'wt0' in ordinary_cli(selected,'--terse','--fields','DEVICE','device','status').splitlines():
  assert time.monotonic()<deadline;time.sleep(.1)
 initial=set(ordinary_cli(selected,'--terse','--fields','UUID','connection','show').splitlines())
 cls=native_wireguard.WireGuardPeer if KIND=='wireguard' else native_openvpn.OpenVPNPeer
 peer=cls(selected,args.nm_role);peer.start(ordinary_cli)
 proof['stages']['connected']=peer.probe();flow();nb_probe(neighbor)
 ordinary_cli(selected,'connection','down',peer.connection)
 if KIND=='openvpn':peer.wait_disconnected()
 proof['stages']['disconnected_neighbor']=nb_probe(neighbor);flow()
 ordinary_cli(selected,'connection','up',peer.connection)
 proof['stages']['reconnected']=peer.probe();nb_probe(neighbor);flow()
 assert records()==rows,'A client operation changed the captured desktop graph'
except BaseException as failure:error=failure
finally:
 failures=[]
 try:
  for identity in imported[args.nm_role]:ordinary_cli(selected,'connection','delete','uuid',identity)
  if peer is not None and KIND=='openvpn':peer.wait_disconnected()
 except BaseException as failure:failures.append('Client retirement: '+type(failure).__name__+': '+str(failure)[-1400:])
 if peer is not None:
  try:peer.close()
  except BaseException as failure:failures.append('Endpoint retirement: '+type(failure).__name__+': '+str(failure)[-1400:])
 try:
  if initial is not None:
   deadline=time.monotonic()+6
   while set(ordinary_cli(selected,'--terse','--fields','UUID','connection','show').splitlines())!=initial:
    assert time.monotonic()<deadline,'The own retired VPN retained connection state';time.sleep(.1)
  nb(selected,'up');deadline=time.monotonic()+25
  while True:
   try:proof['selected_netbird_restored']=nb_probe(selected);break
   except AssertionError:
    flow();assert time.monotonic()<deadline;time.sleep(.5)
  proof['neighbor_netbird_after']=nb_probe(neighbor);proof['payloads_before']=initial_count;proof['payloads_after']=flow()
  assert initial_count is not None and proof['payloads_after']>initial_count
  assert records()==rows
  assert host_snapshot(NetworkCommand())==before
  assert host_config()==before_config
  assert subprocess.check_output(['systemctl','show','--property=MainPID','--value','NetworkManager.service'],text=True).strip()==host_nm
 except BaseException as failure:failures.append('Restoration: '+type(failure).__name__+': '+str(failure)[-1400:])
 current_inv=subprocess.check_output([*user,'systemctl','--user','show',unit,'-p','InvocationID','--value'],text=True).strip()
 if current_inv==inv:subprocess.run([*user,'systemctl','--user','stop',unit],check=True,timeout=10)
 proof.update(neighbor_record_and_existing_tcp_dns_udp_preserved=not failures,fixture_owned_cleanup=not failures,cleanup_errors=failures,passed=error is None and not failures)
 result_file.write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2),flush=True)
 if failures:raise RuntimeError('Own mixed fixture restoration incomplete') from error
if error is not None:raise error
