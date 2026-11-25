#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stock mixed VPN acceptance in an existing disposable graphical family.

Run only on the two named QA hosts after the installed LightDM/family gate has
created root-owned /run/karyo-qa-main/root and state.json with name/work_name.
Both desktops must be live with their native user managers. No user VPN state
or credentials are imported. Retain temporary root-only TLS evidence on the VM.
"""
from pathlib import Path
from types import SimpleNamespace
import ast,hashlib,json,os,socket,subprocess,sys,time

assert os.getuid()==0 and socket.gethostname() in ('karyo-network-debian','karyo-network-ubuntu')
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

# Reuse reviewed ephemeral endpoint/key helpers, but clients use the live desktop
# user manager and normal installed PATH rather than a source fixture service.
native_wireguard.client=client;native_openvpn.client=client
peers=[];initial={role:set(ordinary_cli(c,'--terse','--fields','UUID','connection','show').splitlines()) for role,c in contexts.items()}
proof={'actual_installed_family':True,'unmodified_wireguard_openvpn_and_nm':True,'stages':{}}
error=None
try:
    for role,cls in (('home',native_wireguard.WireGuardPeer),('work',native_openvpn.OpenVPNPeer)):
        c=contexts[role]
        assert not (Path(c.state.account.home)/'wgqa.conf').exists()
        assert not (Path(c.state.account.home)/'openvpn_fixture.ovpn').exists()
        peer=cls(c,role);peers.append(peer)
        peer.start(ordinary_cli)
        proof['stages'][role+'_connected']=peer.probe()
        for existing in peers:existing.probe()
    proof['simultaneous']={p.label:p.probe() for p in peers}
    old=records()
    subprocess.run(['systemctl','restart','karyo-profile-host.service'],check=True,timeout=20)
    assert records()==old,'Controller reopen changed the installed VPN contexts'
    proof['after_controller_reopen']={p.label:p.probe() for p in peers}
    for selected in peers:
        neighbor=next(p for p in peers if p is not selected)
        old_neighbor=neighbor.scope.record
        ordinary_cli(selected.scope,'connection','down',selected.connection)
        if isinstance(selected,native_openvpn.OpenVPNPeer):selected.wait_disconnected()
        proof['stages'][selected.label+'_disconnected_neighbor_alive']=neighbor.probe()
        assert next(r for r in records() if r['token']==old_neighbor['token'])==old_neighbor
        ordinary_cli(selected.scope,'connection','up',selected.connection)
        proof['stages'][selected.label+'_reconnected']=selected.probe()
        neighbor.probe()
except BaseException as failure:
    error=failure
finally:
    failures=[]
    for peer in reversed(peers):
        try:
            for connection in imported[peer.label]:ordinary_cli(peer.scope,'connection','delete','uuid',connection)
            if isinstance(peer,native_openvpn.OpenVPNPeer):peer.wait_disconnected()
        except BaseException as failure:failures.append(type(failure).__name__+': '+str(failure)[-2000:])
        finally:
            try:peer.close()
            except BaseException as failure:failures.append('Peer retirement: '+str(failure)[-2000:])
    for role,c in contexts.items():
        try:
            deadline=time.monotonic()+5
            while set(ordinary_cli(c,'--terse','--fields','UUID','connection','show').splitlines())!=initial[role]:
                assert time.monotonic()<deadline,'The retired '+role+' interface retained connection state'
                time.sleep(.1)
        except BaseException as failure:failures.append(str(failure)[-2000:])
    try:
        after=host_snapshot(NetworkCommand())
        assert after==before,'Host network objects changed after fixture retirement: '+repr([k for k in before if before[k]!=after[k]])
        assert host_config()==before_config,'Host connection configuration changed'
        assert subprocess.check_output(['systemctl','show','--property=MainPID','--value','NetworkManager.service'],text=True).strip()==host_nm
    except BaseException as failure:failures.append(str(failure))
    proof['fixture_cleanup_errors']=failures
    proof['fixture_owned_cleanup']=not failures
    proof['passed']=error is None and not failures
    (root/'installed-mixed.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,indent=2),flush=True)
    if failures:raise RuntimeError('Controlled fixture cleanup incomplete: '+repr(failures)) from error
if error is not None:raise error
