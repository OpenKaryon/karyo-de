#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe a late plain desktop while existing Home/Work VPN streams continue."""
from pathlib import Path
import ast,json,os,socket,subprocess,time
assert os.getuid()==0 and socket.gethostname() in ('karyo-network-debian','karyo-network-ubuntu')
root=Path(Path('/run/karyo-qa-main/root').read_text().strip());state=json.loads((root/'state.json').read_text());continuity=json.loads((root/'late-desktop-continuity.json').read_text())
def records():return json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles']
def prefix(uid,gid):return ['setpriv','--reuid='+str(uid),'--regid='+str(gid),'--init-groups','--inh-caps=-all','--ambient-caps=-all','env','XDG_RUNTIME_DIR=/run/user/'+str(uid),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus']
def flow():
 current=records()
 for before in continuity['before']:assert next(r for r in current if r['token']==before['token'])==before
 values=[json.loads(Path(w['progress']).read_text()) for w in continuity['workers']];assert all(v['errors']==0 and v['count']>=3 for v in values),values
 return [v['count'] for v in values]
initial=flow();rows=records();assert len(rows)==3
third=next(r for r in rows if r['account']['name']==state['experiments_name']);assert third['session']['pending'] is None
assert 'provider' not in third
for row in continuity['before']:
 assert row['network']['namespace']!=third['network']['namespace']
 assert row['dns']['resolver']['runtime'][0]!=third['dns']['resolver']['runtime'][0]
uid,gid=third['account']['uid'],third['account']['gid']
code=r"""import json,socket,subprocess,urllib.request
from pathlib import Path
def identity(path):
 s=Path(path).stat();return [s.st_dev,s.st_ino]
assert identity('/proc/self/ns/net')==NETWORK and identity('/run/systemd/resolve')==RESOLVER
assert socket.getaddrinfo('example.com',443,socket.AF_INET)
with urllib.request.urlopen('https://example.com',timeout=10) as response:assert response.status==200
active=subprocess.check_output(['nmcli','-t','-f','UUID,DEVICE','connection','show','--active'],text=True)
assert TOKEN[:8]+'-'+TOKEN[8:12]+'-'+TOKEN[12:16]+'-'+TOKEN[16:20]+'-'+TOKEN[20:]+':eth0' in active
assert json.loads(subprocess.check_output(['ip','-j','-4','route','get','1.1.1.1'],text=True))[0]['dev']=='eth0'
assert not json.loads(subprocess.check_output(['ip','-j','-6','route','show','default'],text=True))
assert Path('/proc/sys/net/ipv6/conf/all/disable_ipv6').read_text().strip()=='1'
print(json.dumps({'late_desktop_private_nm_dns_https':True,'direct_uplink':True,'documented_ipv4_only_policy':True}))
"""
values={'NETWORK':third['network']['namespace'],'RESOLVER':third['dns']['resolver']['runtime'][0],'TOKEN':third['token']}
code='\n'.join(k+'='+repr(v) for k,v in values.items())+'\n'+code;ast.parse(code)
try:
 result=subprocess.run([*prefix(uid,gid),'systemd-run','--user','--quiet','--wait','--pipe','--collect','--unit=karyo-qa-late-probe','/usr/bin/python3','-I','-c',code],capture_output=True,text=True,timeout=30);assert result.returncode==0,result.stderr[-1800:]
 proof=json.loads(result.stdout);during=flow()
 shells=[]
 for p in Path('/proc').iterdir():
  if not p.name.isdecimal():continue
  try:
   if p.stat().st_uid==uid and (p/'exe').resolve()==Path('/usr/bin/karyo-shell'):shells.append(p)
  except (FileNotFoundError,ProcessLookupError):pass
 assert len(shells)==1
 env=dict(v.split('=',1) for v in (shells[0]/'environ').read_bytes().decode().split('\0') if '='in v)
 parent=env['LABWC_PID'];assert parent.isdecimal() and Path('/proc',parent).stat().st_uid==uid and Path('/proc',parent,'exe').resolve()==Path('/usr/bin/labwc')
 subprocess.run([*prefix(uid,gid),'systemd-run','--user','--quiet','--collect','--unit=karyo-qa-late-logout','--setenv=LABWC_PID='+parent,'/usr/bin/labwc','--exit'],check=True,timeout=10)
 deadline=time.monotonic()+40
 while any(r['token']==third['token'] for r in records()):
  flow();assert time.monotonic()<deadline,'The late graphical desktop did not retire';time.sleep(.2)
 assert len(records())==2
 assert not (Path('/run/netns')/('karyo-'+third['token'])).exists()
 final=flow();assert all(end>start for start,end in zip(initial,final))
 proof.update(ordinary_late_frontend_start=True,natural_late_logout=True,same_neighbors_and_existing_tcp_streams=True,managed_dns_udp_continued=True,payloads_before=initial,payloads_during=during,payloads_after=final,late_vt=third['session']['graphical']['vt'])
 (root/'late-desktop-result.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
finally:
 for worker in continuity['workers']:
  p=prefix(worker['uid'],worker['gid'])
  current=subprocess.check_output([*p,'systemctl','--user','show',worker['unit'],'-p','InvocationID','--value'],text=True).strip()
  if current==worker['invocation']:subprocess.run([*p,'systemctl','--user','stop',worker['unit']],check=True,timeout=10)
