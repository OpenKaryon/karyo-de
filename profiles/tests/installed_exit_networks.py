#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Select an own QA exit node through the installed ordinary desktop CLI."""
from pathlib import Path
import ast,copy,json,os,socket,subprocess,sys,time
assert os.getuid()==os.geteuid()==0 and socket.gethostname() in ('karyo-network-debian','karyo-network-ubuntu')
BASE,DIRECT,EXIT=sys.argv[1:];assert BASE in ('debian','ubuntu')
root=Path(Path('/run/karyo-qa-main/root').read_text().strip());state=json.loads((root/'state.json').read_text())
def records():return json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles']
rows=records();assert len(rows)==2
roles={role:next(r for r in rows if r['account']['name']==state['name' if role=='home' else 'work_name']) for role in ('home','work')}
host_before={key:subprocess.check_output(args) for key,args in {'routes':['ip','-j','-4','route','show','table','all'],'rules':['ip','-j','-4','rule','show'],'resolver':['readlink','-f','/etc/resolv.conf'],'forwarding':['cat','/proc/sys/net/ipv4/ip_forward']}.items()}
def user(role,args):
 row=roles[role];uid,gid=row['account']['uid'],row['account']['gid']
 prefix=['setpriv','--reuid='+str(uid),'--regid='+str(gid),'--init-groups','--inh-caps=-all','--ambient-caps=-all','env','XDG_RUNTIME_DIR=/run/user/'+str(uid),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus','systemd-run','--user','--quiet','--wait','--pipe','--collect','--unit=karyo-qa-selected-exit']
 # systemd resolves ExecStart names before applying the unit PATH; env performs ordinary session lookup.
 if args[0]=='netbird':args=['/usr/bin/env',*args]
 result=subprocess.run([*prefix,*args],capture_output=True,text=True,timeout=35)
 assert result.returncode==0,result.stderr[-1800:]
 return result.stdout
client=r"""import hashlib,json,os,socket,subprocess,urllib.request
from pathlib import Path
def identity(p):
 s=Path(p).stat();return [s.st_dev,s.st_ino]
assert identity('/proc/self/ns/net')==NETWORK and identity('/run/systemd/resolve')==RESOLVER
value=json.loads(subprocess.check_output(['netbird','status','--json'],text=True,timeout=10));assert value['management']['connected'] and value['signal']['connected']
assert socket.getaddrinfo('example.com',443,socket.AF_INET)
with urllib.request.urlopen('https://example.com',timeout=10) as response:assert response.status==200
# Distinct expected source addresses make the remote egress, not merely a route, observable.
data=os.urandom(512)
with socket.create_connection(('192.168.230.163',43872),timeout=8) as stream:
 stream.sendall(data);answer=b''
 while not answer.endswith(b'\n'):
  block=stream.recv(2048);assert block;answer+=block
 tcp=json.loads(answer)
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
 udp.settimeout(8);udp.sendto(data,('192.168.230.163',43872));answer,_=udp.recvfrom(2048);packet=json.loads(answer)
for reply in (tcp,packet):assert reply=={'peer':EXPECTED,'digest':hashlib.sha256(data).hexdigest(),'host':'karyo-de-acceptance'},reply
route=json.loads(subprocess.check_output(['ip','-j','-4','route','get','1.1.1.1'],text=True));assert (route[0]['dev']=='wt0')==SELECTED,route
print(json.dumps({'source_seen_by_remote':EXPECTED,'dns_https':True,'nonce_tcp_udp':True,'route_device':route[0]['dev']}))
"""
def probe(role,expected,selected):
 row=roles[role];values={'NETWORK':row['network']['namespace'],'RESOLVER':row['dns']['resolver']['runtime'][0],'EXPECTED':expected,'SELECTED':selected}
 code='\n'.join(k+'='+repr(v) for k,v in values.items())+'\n'+client;ast.parse(code)
 return json.loads(user(role,['/usr/bin/python3','-I','-c',code]))
network='karyo-qa-'+BASE+'-home-exit'
proof={'before':{role:probe(role,DIRECT,False) for role in roles}}
listing=user('home',['netbird','networks','list']);assert network in listing
try:
 user('home',['netbird','networks','select',network]);deadline=time.monotonic()+35
 while True:
  try:proof['selected']={'home':probe('home',EXIT,True),'work':probe('work',DIRECT,False)};break
  except AssertionError:
   assert time.monotonic()<deadline;time.sleep(.5)
 assert records()==rows,'Selection changed the captured desktop contexts'
finally:user('home',['netbird','networks','deselect',network])
proof['after']={role:probe(role,DIRECT,False) for role in roles}
for key,args in {'routes':['ip','-j','-4','route','show','table','all'],'rules':['ip','-j','-4','rule','show'],'resolver':['readlink','-f','/etc/resolv.conf'],'forwarding':['cat','/proc/sys/net/ipv4/ip_forward']}.items():assert subprocess.check_output(args)==host_before[key],key
assert records()==rows
proof.update(ordinary_selection_and_deselection=True,host_and_neighbor_unchanged=True)
(root/'selected-netbird-exit.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
