#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual installed main logout while one Work VPN TCP connection stays open.

Use only the two disposable QA hosts with an already prepared root-owned family
fixture at /run/karyo-qa-main/root. Pass the other guest's own endpoint JSON
(ip, port, host, fqdn). That ordinary Work server must echo the hostname and
SHA256 of every 512-byte TCP/UDP payload, keeping the TCP stream open. No user
credentials, namespace migration or manufactured login cgroup are used.
"""
from pathlib import Path
import ast,json,os,socket,subprocess,sys,time
assert os.getuid()==os.geteuid()==0 and socket.gethostname() in ('karyo-network-debian','karyo-network-ubuntu')
assert len(sys.argv)==2
PEER_ENDPOINT=json.loads(sys.argv[1])
sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.display_manager import require_partition
from karyo_profiles.logind import Sessions
from karyo_profiles.networks import NetworkSpec
root=Path(Path('/run/karyo-qa-main/root').read_text().strip());state=json.loads((root/'state.json').read_text())
def records():return json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles']
rows=records();assert len(rows)==2
home=next(r for r in rows if r['account']['name']==state['name']);work=next(r for r in rows if r['account']['name']==state['work_name'])
require_partition();assert Sessions().get(home['main_login']['login']['id']).vt>=13
uid,gid=work['account']['uid'],work['account']['gid']
progress=Path('/run/user')/str(uid)/'karyo-qa-main-survival.json';assert not progress.exists()
prefix=['setpriv','--reuid='+str(uid),'--regid='+str(gid),'--init-groups','--inh-caps=-all','--ambient-caps=-all','env',
    'XDG_RUNTIME_DIR=/run/user/'+str(uid),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus']
code='''import hashlib,json,os,socket,subprocess,time
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
values={'PROGRESS':str(progress),'NETWORK':work['network']['namespace'],'RESOLVER':work['dns']['resolver']['runtime'][0],'PEER':PEER_ENDPOINT}
code='\n'.join(k+'='+repr(v) for k,v in values.items())+'\n'+code;ast.parse(code)
unit='karyo-qa-work-main-survival.service'
subprocess.run([*prefix,'systemd-run','--user','--quiet','--collect','--unit='+unit,'/usr/bin/python3','-I','-c',code],check=True,timeout=10)
invocation=subprocess.check_output([*prefix,'systemctl','--user','show',unit,'--property=InvocationID','--value'],text=True).strip();assert len(invocation)==32
state['netbird_survival_unit']={'unit':unit,'uid':uid,'invocation':invocation,'progress':str(progress)}
(root/'state.json').write_text(json.dumps(state,indent=2)+'\n')
def flow():
    value=json.loads(progress.read_text());assert value['errors']==0,value
    assert next(r for r in records() if r['token']==work['token'])==work,'The main cycle altered Work'
    return value
try:
    deadline=time.monotonic()+10
    while not progress.exists() or flow()['count']<3:
        assert time.monotonic()<deadline,'The existing Work stream was not ready';time.sleep(.1)
    initial=flow()['count'];(root/'main-logout-survival-before.json').write_text(json.dumps({'home':home,'work':work},indent=2)+'\n')
    shells=[]
    for p in Path('/proc').iterdir():
        if not p.name.isdecimal():continue
        try:
            if p.stat().st_uid==home['account']['uid'] and (p/'exe').resolve()==Path('/usr/bin/karyo-shell'):shells.append(p)
        except (FileNotFoundError,ProcessLookupError):pass
    assert len(shells)==1
    env=dict(v.split('=',1) for v in (shells[0]/'environ').read_bytes().decode().split('\0') if '='in v)
    parent=env['LABWC_PID'];hu,hg=home['account']['uid'],home['account']['gid']
    assert parent.isdecimal() and Path('/proc',parent).stat().st_uid==hu and Path('/proc',parent,'exe').resolve()==Path('/usr/bin/labwc')
    hp=['setpriv','--reuid='+str(hu),'--regid='+str(hg),'--init-groups','--inh-caps=-all','--ambient-caps=-all','env',
    'XDG_RUNTIME_DIR=/run/user/'+str(hu),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(hu)+'/bus']
    subprocess.run([*hp,'systemd-run','--user','--quiet','--collect','--unit=karyo-qa-main-normal-logout','--setenv=LABWC_PID='+parent,'/usr/bin/labwc','--exit'],check=True,timeout=10)
    deadline=time.monotonic()+45;greeters=set()
    while any(r['token']==home['token'] for r in records()):
        flow();assert time.monotonic()<deadline,'Normal Home logout did not finish'
        for greeter in Sessions().greeters():
            assert greeter.vt>=13;greeters.add(greeter.vt)
        time.sleep(.2)
    assert not (Path('/run/netns')/NetworkSpec(hu,home['token']).name).exists()
    at_logout=flow()['count']
    if not any(r['account']['uid']==hu for r in records()):
        subprocess.run(['systemctl','stop','lightdm.service'],check=True,timeout=25);flow()
        subprocess.run(['systemctl','start','lightdm.service'],check=True,timeout=25)
    deadline=time.monotonic()+45
    while True:
        fresh=next((r for r in records() if r['account']['uid']==hu),None)
        if fresh and 'main_login'in fresh and fresh['main_login']['pending'] is None:break
        flow();assert time.monotonic()<deadline,'The fresh Home entry did not attach';time.sleep(.2)
    require_partition();new_vt=Sessions().get(fresh['main_login']['login']['id']).vt;assert new_vt>=13
    assert fresh['token']!=home['token'] and fresh['network']['indices'][0]!=home['network']['indices'][0]
    deadline=time.monotonic()+5
    while flow()['count']<=at_logout+3:
        assert time.monotonic()<deadline;time.sleep(.1)
    final=flow()
    proof={'ordinary_main_logout':True,'fresh_main_entry':True,'main_vt':new_vt,'work_vt':work['session']['graphical']['vt'],
    'observed_greeter_vts':sorted(greeters),'same_work_graph_and_provider':True,'one_existing_encrypted_tcp_connection_survived':True,
    'private_dns_and_udp_survived':True,'payloads_before':initial,'payloads_at_logout':at_logout,'payloads_after':final['count']}
    (root/'main-logout-survival.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,indent=2))
finally:
    current=subprocess.check_output([*prefix,'systemctl','--user','show',unit,'--property=InvocationID','--value'],text=True).strip()
    if current==invocation:subprocess.run([*prefix,'systemctl','--user','stop',unit],check=True,timeout=10)
