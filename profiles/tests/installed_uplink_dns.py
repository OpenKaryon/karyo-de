#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary physical DNS update beside two already connected desktop VPNs.

Use only the named disposable native QA guests with their captured temporary
family and remote nonce endpoint. Keep the production controller running; read
its journal snapshots without opening a second journal owner. Restore only the
captured physical link domains, and stop only matching fixture invocations.
Pass the temporary peer JSON and a fresh eight-hex-digit result label.
"""
from pathlib import Path
from copy import deepcopy
import ast,hashlib,json,os,secrets,socket,subprocess,sys,time

assert os.getuid()==0 and socket.gethostname() in ('karyo-network-debian','karyo-network-ubuntu')
assert len(sys.argv)==3
peer=json.loads(sys.argv[1]);label=sys.argv[2]
assert len(label)==8 and all(c in '0123456789abcdef' for c in label)
# Keep the original socket across the carrier transition. UDP has its own
# finite retry budget: a dropped datagram does not prove a broken TCP stream.
import inspect
from network_probe_support import udp_exchange, write_networkd_dropin, CARRIER_CONTINUITY_CODE
continuity=inspect.getsource(udp_exchange)+'\n'+CARRIER_CONTINUITY_CODE
ast.parse(continuity)
sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.dns import ResolverPlan
from karyo_profiles.journal_dns_record import seed
from karyo_profiles.networks import NetworkSpec

root=Path(Path('/run/karyo-qa-main/root').read_text().strip())
result_file=root/('uplink-dns-result-'+label+'.json');assert not result_file.exists()
state=json.loads((root/'state.json').read_text())
def records():return json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles']
before=records();assert len(before)==2
contexts={role:next(p for p in before if p['account']['name']==state['name' if role=='home' else 'work_name'])
          for role in ('home','work')}

def call(address,method,*args):
    return json.loads(subprocess.check_output(['/usr/bin/busctl',address,'--json=short','--timeout=3s',*method,*args],text=True))['data']

def link_path(address,index):
    return call(address,('call','org.freedesktop.resolve1','/org/freedesktop/resolve1',
                        'org.freedesktop.resolve1.Manager','GetLink'),'i',str(index))[0]

def property(address,path,name):
    return call(address,('get-property','org.freedesktop.resolve1',path,'org.freedesktop.resolve1.Link',name))

physical=discover_underlay();assert physical.interface=='ens160'
host='--system';host_path=link_path(host,physical.index)
original=property(host,host_path,'Domains')
marker='karyo-qa-uplink.invalid';assert all(p[0]!=marker for p in original)
target=None
owner=call(host,('call','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetNameOwner'),
           's','org.freedesktop.resolve1')[0]

network_file=Path('/run/systemd/network')/('10-netplan-qa.network' if socket.gethostname()=='karyo-network-debian'
                                         else 'zzzz-dracut-default.network')
assert network_file.is_file() and network_file.stat().st_uid==0
network_hash=hashlib.sha256(network_file.read_bytes()).hexdigest()
drop_directory=network_file.with_name(network_file.name+'.d');created_directory=False
drop=drop_directory/'95-karyo-qa-uplink-dns.conf';assert not drop.exists()
drop_text='[Network]\nDomains='+marker+'\n';drop_identity=None
serial=secrets.token_hex(4)
(root/('uplink-dns-before-'+serial+'.json')).write_text(json.dumps(before,indent=2)+'\n')

physical_link=json.loads(subprocess.check_output(['ip','-j','link','show','dev',physical.interface],text=True))[0]

def reconfigure(*,restoring=False):
    if not restoring:assert discover_underlay()==physical
    live=json.loads(subprocess.check_output(['ip','-j','link','show','dev',physical.interface],text=True))[0]
    assert (live['ifindex'],live['address'])==(physical_link['ifindex'],physical_link['address'])
    assert call(host,('call','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetNameOwner'),
                's','org.freedesktop.resolve1')[0]==owner
    assert hashlib.sha256(network_file.read_bytes()).hexdigest()==network_hash
    # Networkd owns this link. Update its own runtime drop-in instead of calling
    # resolved's rejected SetLinkDomains path for a managed physical interface.
    subprocess.run(['networkctl','reload'],check=True,timeout=10)
    subprocess.run(['networkctl','reconfigure',physical.interface],check=True,timeout=10)

def wait_host(present,expected=None):
    deadline=time.monotonic()+15
    while True:
        current=property(host,host_path,'Domains')
        if (any(p[0]==marker for p in current)==present and
                (expected is None or {tuple(p) for p in current}=={tuple(p) for p in expected})):return current
        assert time.monotonic()<deadline,'Networkd did not update physical search domains'
        time.sleep(.2)

def private_links(row):
    spec=NetworkSpec(row['account']['uid'],row['token'])
    plan=ResolverPlan(spec,seed(row['dns']['seed']),row['account']['gid'],row['dns']['service_uid'],'/run/karyo/validation-only')
    address='--address='+plan.bus_address
    pid=row['dns']['resolver']['pid'];result={}
    for name in ('eth0','wt0'):
        link=json.loads(subprocess.check_output(['nsenter','--net=/proc/'+str(pid)+'/ns/net',
                                                 '/usr/sbin/ip','-j','link','show','dev',name],text=True))[0]
        path=link_path(address,link['ifindex'])
        result[name]={key:property(address,path,key) for key in ('DNS','Domains','DefaultRoute')}
    return result

# A previous physical update may have finished on the host before the bounded
# desktop watcher consumed its last inventory. Begin from a converged baseline.
deadline=time.monotonic()+40
while True:
    rows=records()
    if all(p['dns']['seed']['search']==[v[0] for v in original if not v[1]] and
           'refresh' not in p['dns'] and 'refresh' not in p['forwarding'] for p in rows):break
    assert time.monotonic()<deadline,'Wait for the existing physical DNS update before starting this fixture'
    time.sleep(.2)
before=rows
contexts={role:next(p for p in before if p['account']['name']==state['name' if role=='home' else 'work_name'])
          for role in ('home','work')}
private_before={role:private_links(row) for role,row in contexts.items()}
(root/('uplink-dns-before-'+serial+'.json')).write_text(json.dumps(before,indent=2)+'\n')
units={};counts={}
def prefix(row):
    uid,gid=row['account']['uid'],row['account']['gid']
    return ['setpriv','--reuid='+str(uid),'--regid='+str(gid),'--init-groups','--inh-caps=-all','--ambient-caps=-all',
            'env','XDG_RUNTIME_DIR=/run/user/'+str(uid),'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus']
def flows():
    values={}
    for role,(row,unit,invocation,path) in units.items():
        value=json.loads(path.read_text());assert value['errors']==0,value
        values[role]=value['count']
    return values

def logical_receipt(row):
    result=deepcopy(row)
    # A journaled carrier transition atomically republishes the same fixed nft
    # rules, receiving fresh rule handles. Table and chain identities stay fixed.
    for item in result['forwarding']['proof']['firewall']['snapshot']:
        if 'rule' in item:item['rule'].pop('handle',None)
    return result

def wait_for_domains(expected,present):
    deadline=time.monotonic()+40
    while True:
        flows();current=records();assert len(current)==len(before)
        done=True
        for old in before:
            new=next(p for p in current if p['token']==old['token'])
            remaining=deepcopy(new);remaining['dns']['seed']=old['dns']['seed']
            # A refresh intent can exist briefly; it may only describe this DNS update.
            remaining['dns'].pop('refresh',None)
            remaining['forwarding'].pop('refresh',None)
            assert logical_receipt(remaining)==logical_receipt(old),'A physical DNS update replaced another desktop resource'
            done=done and ('refresh' not in new['dns']) and ('refresh' not in new['forwarding']) and ((marker in new['dns']['seed']['search'])==present)
            done=done and new['dns']['seed']['search']==[p[0] for p in expected if not p[1]]
        if done:
            for role,row in contexts.items():
                links=private_links(row)
                assert links['wt0']==private_before[role]['wt0'],'The VPN DNS link changed'
                assert links['eth0']['DNS']==private_before[role]['eth0']['DNS']
                assert links['eth0']['DefaultRoute']==private_before[role]['eth0']['DefaultRoute']
                assert links['eth0']['Domains']==expected
            return
        assert time.monotonic()<deadline,'The installed physical DNS watcher did not finish'
        time.sleep(.2)

def advancing(previous):
    deadline=time.monotonic()+35
    while True:
        current=flows()
        if all(current[r]>previous[r] for r in contexts):return current
        assert time.monotonic()<deadline,'The existing encrypted streams did not resume'
        time.sleep(.1)

try:
    for role,row in contexts.items():
        path=Path('/run/user')/str(row['account']['uid'])/('karyo-qa-uplink-dns-'+serial+'.json')
        assert not path.exists()
        code='\n'.join(k+'='+repr(v) for k,v in dict(PROGRESS=str(path),NETWORK=row['network']['namespace'],
                    RESOLVER=row['dns']['resolver']['runtime'][0],PEER=peer).items())+'\n'+continuity
        ast.parse(code);unit='karyo-qa-uplink-continuity-'+serial+'.service'
        subprocess.run([*prefix(row),'systemd-run','--user','--quiet','--collect','--unit='+unit,
                        '/usr/bin/python3','-I','-c',code],check=True,timeout=10)
        inv=subprocess.check_output([*prefix(row),'systemctl','--user','show',unit,'-p','InvocationID','--value'],text=True).strip()
        assert len(inv)==32;units[role]=(row,unit,inv,path)
        (root/('uplink-dns-'+role+'-unit.json')).write_text(json.dumps({'unit':unit,'invocation':inv,'uid':row['account']['uid'],'progress':str(path)})+'\n')
    deadline=time.monotonic()+10
    while not all(v[3].exists() for v in units.values()) or min(flows().values())<3:
        assert time.monotonic()<deadline;time.sleep(.1)
    counts['before']=flows();created_directory,drop_identity=write_networkd_dropin(drop,drop_text)
    reconfigure();target=wait_host(True,original+[[marker,False]])
    wait_for_domains([p for p in target if not p[1]],True)
    counts['updated']=advancing(counts['before'])
    assert all(counts['updated'][r]>counts['before'][r] for r in contexts)
finally:
    try:
        if drop_identity is not None:
            node=drop.stat();assert (node.st_dev,node.st_ino)==drop_identity and drop.read_text()==drop_text
            drop.unlink()
            if created_directory:drop_directory.rmdir()
            reconfigure(restoring=True)
        assert wait_host(False,original)==original
        if drop_identity is not None and units and all(v[3].exists() for v in units.values()):
            wait_for_domains(private_before['home']['eth0']['Domains'],False)
            if 'updated' in counts:counts['restored']=advancing(counts['updated'])
    finally:
        for row,unit,inv,path in units.values():
            actual=subprocess.check_output([*prefix(row),'systemctl','--user','show',unit,'-p','InvocationID','--value'],text=True).strip()
            if actual==inv:subprocess.run([*prefix(row),'systemctl','--user','stop',unit],check=True,timeout=10)
after=records()
(root/('uplink-dns-after-'+serial+'.json')).write_text(json.dumps(after,indent=2)+'\n')
def changed_paths(a,b,path=''):
    if isinstance(a,dict) and isinstance(b,dict):
        return [p for k in sorted(set(a)|set(b)) for p in changed_paths(a.get(k),b.get(k),path+'/'+str(k))]
    if isinstance(a,list) and isinstance(b,list) and len(a)==len(b):
        return [p for i,(x,y) in enumerate(zip(a,b)) for p in changed_paths(x,y,path+'/'+str(i))]
    return [] if a==b else [path]
changes=changed_paths([logical_receipt(row) for row in before],[logical_receipt(row) for row in after])
assert not changes,'Final receipt differences: '+str(changes)
assert all(counts['restored'][r]>counts['updated'][r] for r in contexts)
proof={'physical_dns_update_and_restore':True,'private_vpn_dns_links_unchanged':True,
       'same_namespaces_resolvers_workers':True,'existing_home_work_encrypted_tcp_dns_udp_survived':True,
       'physical_route_and_final_journal_unchanged':True,'payloads':counts,
       'transport_observations':{role:json.loads(path.read_text()) for role,(_,_,_,path) in units.items()},
       'udp_retried_within_finite_budget':True,
       'scope':'Networkd reconfigure and same-address DHCP reacquisition with a bounded carrier pause; a different interface/gateway is a separate test'}
result_file.write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
