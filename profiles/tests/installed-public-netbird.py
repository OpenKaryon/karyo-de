#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host public VPN startup and ordinary client acceptance."""
import asyncio,hashlib,json,os,shutil,signal,socket,stat,subprocess,sys,time,uuid
from pathlib import Path

async def peer_server(ip):
    import ipaddress
    require_ip=ipaddress.IPv4Address(ip)
    assert os.getuid()==os.geteuid()==1002 and require_ip in ipaddress.IPv4Network('100.64.0.0/10')
    assert os.environ.get('XDG_RUNTIME_DIR')=='/run/user/1002'
    proof=Path('/run/user/1002/karyo-qa-home-peer-incoming.json')
    counts={'tcp':0,'udp':0};fd=os.open(proof,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd)
    def record(kind):
        counts[kind]+=1;proof.write_text(json.dumps(counts))
    def answer(data):return b'karyo-work-peer:'+hashlib.sha256(data).digest()
    async def accept(reader,writer):
        try:
            data=await asyncio.wait_for(reader.readexactly(65536),8)
            writer.write(answer(data));await writer.drain();record('tcp')
        finally:writer.close();await writer.wait_closed()
    class UDP(asyncio.DatagramProtocol):
        def connection_made(self,transport):self.transport=transport
        def datagram_received(self,data,peer):self.transport.sendto(answer(data),peer);record('udp')
    tcp=await asyncio.start_server(accept,ip,43861)
    transport,_=await asyncio.get_running_loop().create_datagram_endpoint(UDP,local_addr=(ip,43861))
    try:
        async with tcp:await tcp.serve_forever()
    finally:transport.close()

if sys.argv[1:2]==['--peer-server']:
    assert len(sys.argv)==3 and socket.gethostname() in {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'}
    asyncio.run(peer_server(sys.argv[2]));raise SystemExit(0)
if (os.getuid()!=0 or os.geteuid()!=0 or socket.gethostname() not in
        {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} or sys.argv[1:] not in ([],['--missing'],['--connected'],['--pkce'],['--home-vpn'],['--home-vpn-reconnect'])):
    raise SystemExit('Use only the fixed disposable hosts as root')
sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.consoles import KernelConsoles
from karyo_profiles.journal_session import stat_start
from karyo_profiles.logind import Sessions
from karyo_profiles.model import DesktopSettings
from karyo_profiles.runtime_policy import enabled,forwarding_ready
from karyo_profiles.storage import ConfigStore


def require(value,message):
    if not value:raise RuntimeError(message)


def run(*args):return subprocess.check_output(args,text=True,timeout=25).strip()


def snapshot():
    return {k:run(*a) for k,a in {
        'routes':('/usr/sbin/ip','-4','-j','-N','route','show','table','all'),
        'rules':('/usr/sbin/ip','-4','-j','-N','rule','show'),
        'nft':('/usr/sbin/nft','-j','list','ruleset')}.items()}|{
        'resolver':os.readlink('/etc/resolv.conf'),
        'resolver_sha':hashlib.sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest()}


def journal():
    p=Path('/run/karyo/profile-host/state.json');s=p.lstat()
    require(stat.S_ISREG(s.st_mode) and s.st_uid==0 and stat.S_IMODE(s.st_mode)==0o600 and s.st_nlink==1,'Unsafe journal')
    return json.loads(p.read_bytes())


def wait_for(predicate,message,seconds=40):
    deadline=time.monotonic()+seconds
    while not predicate():
        require(time.monotonic()<deadline,message);time.sleep(.1)


def request(source,target):
    return subprocess.run(['/usr/local/libexec/karyo-qa-public-runtime','--helper',source,target],capture_output=True,text=True,timeout=65)


def switch(source,target):
    result=request(source,target);require(result.returncode==0,'Public switch failed: '+result.stderr)
    value=json.loads(result.stdout);require(value['user']==target and value['changed'],'Wrong public target');return value


def user(*args):
    return ['/usr/sbin/runuser','-u','kq-switch-work','--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/1002',
            'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus',*args]


def cli(*args):
    name='karyo-qa-public-netbird-'+uuid.uuid4().hex+'.service'
    return subprocess.run(user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect','--unit='+name,'--',
                               '/usr/bin/karyo-netbird-cli',*args),capture_output=True,text=True,timeout=70)


def status():
    result=cli('status','--json');require(result.returncode==0,'Ordinary Work CLI failed')
    return json.loads(result.stdout)


reconnect=sys.argv[1:]==['--home-vpn-reconnect']
homevpn=sys.argv[1:] in (['--home-vpn'],['--home-vpn-reconnect'])
missing=sys.argv[1:]==['--missing'];pkce=sys.argv[1:]==['--pkce'];connected=sys.argv[1:] in (['--connected'],['--pkce'],['--home-vpn'],['--home-vpn-reconnect'])
helper=Path('/usr/local/libexec/karyo-qa-public-runtime');info=helper.lstat()
require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and not info.st_mode&0o022 and
        hashlib.sha256(helper.read_bytes()).hexdigest()=='3be27e4ef96458f2400d7ea68058db6cdfe40916c4299ecdb68d152cedea1d83','Fixed context helper changed')
require(enabled(),'Explicit runtime activation required');forwarding_ready()
require(not journal()['profiles'],'Prior runtime requires review')
if homevpn:
    require(run('/usr/sbin/ip','netns','list').split()[0]=='karyo-qa-home-wg' and len(run('/usr/sbin/ip','netns','list').splitlines())==1,'Unexpected Home namespace')
    subprocess.run(['/usr/local/libexec/karyo-qa-home-wireguard','probe'],check=True)
else:require(not run('/usr/sbin/ip','netns','list'),'Prior namespace requires review')
require(not os.path.lexists('/var/lib/karyo/profile-vpn/1002'),'Prior provider state requires review')
require(not os.path.lexists('/usr/bin/netbird') if missing else Path('/usr/bin/netbird').is_file(),'Unexpected vendor fixture')
store,accounts,sessions=ConfigStore(),LocalAccounts(),Sessions()
source=next(s for s in sessions.all() if s.name=='karyoqa' and s.uid==1001 and s.active)
original=snapshot();original_accounts={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [Path('/etc/passwd'),Path('/etc/shadow'),Path('/etc/group'),Path('/etc/gshadow'),*Path('/etc/pam.d').glob('common-*')]}
run('/usr/local/libexec/karyo-qa-two-sessions','prepare');child=accounts.account('kq-switch-work');require(child.uid==1002,'Wrong child')
original_policy=store.settings(child.name);policy=DesktopSettings(vpn='netbird')
state_identity=keyfile=key_identity=key=None
with store.locked():store.save_settings(child.name,policy)
try:
    if missing:
        before=snapshot();result=request('karyoqa',child.name)
        require(result.returncode==1 and 'optional Karyo NetBird provider and client packages' in result.stderr,
                'Public missing-provider response is not actionable')
        require(not journal()['profiles'] and snapshot()==before and not run('/usr/sbin/ip','netns','list'),
                'Missing provider allocated resources')
        require(not os.path.lexists('/var/lib/karyo/profile-vpn/1002') and not os.path.lexists('/run/user/1002'),
                'Missing provider prepared a manager or provider state')
        print(json.dumps({'missing_provider':'rejected before allocation','public_message':'actionable'}),flush=True)
    else:
        modes=('logout','crash','locked-crash') if not connected else ('logout',)
        previous_service=None
        for mode in modes:
            selected=DesktopSettings(vpn='netbird',password='home' if mode=='locked-crash' else 'none')
            with store.locked():store.save_settings(child.name,selected)
            cold=switch('karyoqa',child.name);require(cold.get('starting') is True,'Public startup did not create Work')
            data=journal();require(data['schema']==7 and len(data['profiles'])==1,'Wrong public journal')
            record=data['profiles'][0];provider=record['provider'];graph=record['session']['graphical']
            require(provider['kind']=='netbird' and provider['pending'] is None and record['network']['contents']=='tunnel'
                    and record['session']['catalog']=='netbird' and 'forwarding' in record,'Public graph lacks captured provider/catalog')
            identity=tuple(provider['state']['directory'])
            if state_identity is None:state_identity=identity
            else:require(state_identity==identity,'Provider persistent state changed between cold launches')
            native=provider['service']['unit'];require(previous_service!=native['invocation'],'Cold launch reused old service')
            previous_service=native['invocation']
            value=status();require(value.get('daemonStatus')=='NeedsLogin','Fresh public provider is not awaiting login')
            env=Path('/proc',str(record['session']['manager']['live']['pid']),'environ').read_bytes().split(bytes([0]))
            require(b'XDG_DATA_DIRS=/usr/share/karyo/profile-catalog/netbird:/usr/local/share:/usr/share' in env
                    and any(e.startswith(b'KARYO_NETBIRD_ENDPOINT=') for e in env),'Public manager missed provider environment')
            home=subprocess.run(['/usr/sbin/runuser','-u','karyoqa','--','/usr/bin/karyo-netbird-cli','status','--json'],capture_output=True,text=True,timeout=10)
            require(home.returncode==1,'Unscoped Home client reached Work provider')
            if pkce:
                subprocess.run(['/usr/local/libexec/karyo-qa-netbird-pkce'],check=True,timeout=240)
                require(status().get('daemonStatus')=='Connected','Real browser PKCE did not enroll Work')
            elif connected:
                admission=Path('/var/lib/karyo-qa-netbird-admission/setup-key');s=admission.lstat()
                require(stat.S_ISREG(s.st_mode) and s.st_uid==s.st_gid==0 and stat.S_IMODE(s.st_mode)==0o600 and s.st_nlink==1 and s.st_size<65,'Unsafe lab admission')
                key=admission.read_bytes();uuid.UUID(key.decode().strip());keyfile=Path(child.home)/'.karyo-qa-public-setup-key'
                fd=os.open(keyfile,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
                try:os.fchown(fd,1002,1002);os.write(fd,key);os.fsync(fd)
                finally:os.close(fd)
                key_identity=(keyfile.stat().st_dev,keyfile.stat().st_ino)
                result=cli('up','--management-url','http://192.168.230.163:18443','--setup-key-file',str(keyfile),'--hostname','karyo-qa-public-'+socket.gethostname())
                require(result.returncode==0 and status().get('daemonStatus')=='Connected','Public ordinary enrollment failed')
            if homevpn:
                subprocess.run(['/usr/local/libexec/karyo-qa-home-wireguard','probe'],check=True)
                if reconnect:
                    # Use the existing ordinary Work manager; never run the probe
                    # in Home's root namespace or replace the connected daemon.
                    code="import hashlib,ipaddress,json,os,socket,subprocess;from pathlib import Path;assert socket.getaddrinfo('example.com',443,socket.AF_INET);hidden=False\ntry:socket.gethostbyname('home-tunnel.karyo.test')\nexcept socket.gaierror:hidden=True\nassert hidden;r=subprocess.run(['/usr/bin/curl','-4','--fail','--silent','--show-error','--max-time','15','https://api.ipify.org'],capture_output=True,text=True);assert r.returncode==0;ipaddress.IPv4Address(r.stdout.strip());print(json.dumps({'dns_https':True,'private_home_zone_hidden':True,'namespace':os.readlink('/proc/self/ns/net'),'resolver_sha256':hashlib.sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest()}))"
                    def live_work():
                        require(status().get('daemonStatus')=='Connected','Live Work provider disconnected')
                        result=json.loads(run(*user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect',
                            '--unit=karyo-qa-vpn-reconnect-'+uuid.uuid4().hex+'.service','--','/usr/bin/python3','-I','-c',code)))
                        require(result['namespace']!=os.readlink('/proc/self/ns/net'),'Probe escaped Work network')
                        current=journal()['profiles'][0]
                        require(current['token']==record['token'] and current['provider']==record['provider'] and current['session']==record['session'],'Work graph replaced')
                        return result
                    before_cycle=live_work()
                    for cycle in range(2):
                        run('/usr/local/libexec/karyo-qa-home-wireguard','disconnect')
                        try:
                            run('/usr/local/libexec/karyo-qa-home-wireguard','probe-disconnected')
                            require(live_work()==before_cycle,'Work network/DNS changed while Home disconnected')
                        finally:
                            run('/usr/local/libexec/karyo-qa-home-wireguard','reconnect')
                        run('/usr/local/libexec/karyo-qa-home-wireguard','probe')
                        require(live_work()==before_cycle,'Work network/DNS changed after Home reconnected')
                        print(json.dumps({'home_vpn_reconnect_cycle':cycle+1,'work_connected_dns_https':True,'work_graph_reused':True,'home_tunnel_restored':True}),flush=True)
                peer_actor=Path('/usr/local/libexec/karyo-qa-netbird-peers')
                require(peer_actor.is_file() and peer_actor.stat().st_uid==0 and hashlib.sha256(peer_actor.read_bytes()).hexdigest()=='0349066f637b1698801534bc51fac56630ba57d80f16ebd7250f53fe6f742ea9','Peer QA actor missing or changed')
                own=status();own_ip=own['netbirdIp'].split('/')[0]
                unit='karyo-qa-public-home-peer.service'
                run(*user('/usr/bin/systemd-run','--user','--collect','--unit='+unit,'--property=Type=exec','--',
                          '/usr/bin/python3','-I',__file__,'--peer-server',own_ip))
                import urllib.request
                control_path=Path('/var/lib/karyo-qa-netbird-admission/peer-control.json');info=control_path.lstat()
                require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and info.st_nlink==1 and stat.S_IMODE(info.st_mode)==0o600 and info.st_size<4096,'Unsafe QA peer control')
                control=json.loads(control_path.read_bytes());require(control['url']=='http://192.168.230.163:18443' and control['token'].startswith('nbp_'),'Unexpected QA realm')
                counterpart='karyo-de-ubuntu-acceptance' if socket.gethostname()=='karyo-de-acceptance' else 'karyo-de-acceptance'
                deadline=time.monotonic()+70;peer_result=None;last_keys=[]
                while peer_result is None:
                    peers=status().get('peers',{}).get('details',[])
                    req=urllib.request.Request(control['url']+'/api/peers',headers={'Authorization':'Token '+control['token']})
                    with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(req,timeout=10) as response:live=json.loads(response.read())
                    candidates=[p for p in live if p.get('connected') is True and p.get('hostname')==counterpart]
                    for peer in peers:
                        last_keys=sorted(peer)
                        if len(candidates)!=1 or peer.get('netbirdIp')!=candidates[0]['ip']:continue
                        remote_ip=peer.get('netbirdIp',peer.get('ip','')).split('/')[0]
                        fqdn=peer.get('fqdn','')
                        if remote_ip==own_ip or not fqdn.endswith('.netbird.selfhosted'):continue
                        try:
                            output=run(*user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect',
                                '--unit=karyo-qa-home-peer-client-'+uuid.uuid4().hex+'.service','--','/usr/bin/python3','-I',str(peer_actor),
                                '--client',remote_ip,fqdn,uuid.uuid4().hex+uuid.uuid4().hex))
                            result_peer=json.loads(output)
                            if all(result_peer.get(key) is True for key in ('tcp_ip','udp_ip','tcp_dns','udp_dns')):peer_result=result_peer;break
                        except (subprocess.SubprocessError,ValueError):pass
                    require(time.monotonic()<deadline,'No mutual Home-VPN NetBird payload; peer keys='+repr(last_keys))
                    if peer_result is None:time.sleep(.2)
                print(json.dumps({'simultaneous_netbird_peer_payload':peer_result}),flush=True)
                def reciprocal():
                    path=Path('/run/user/1002/karyo-qa-home-peer-incoming.json')
                    try:
                        info=path.lstat();require(stat.S_ISREG(info.st_mode) and info.st_uid==1002 and info.st_nlink==1 and stat.S_IMODE(info.st_mode)==0o600,'Unsafe reciprocal proof')
                        counts=json.loads(path.read_bytes());return counts.get('tcp',0)>=2 and counts.get('udp',0)>=2
                    except (FileNotFoundError,ValueError):return False
                wait_for(reciprocal,'Other peer did not complete reciprocal TCP/UDP',seconds=70)
                code="import ipaddress,json,socket,subprocess;resolved=socket.getaddrinfo('example.com',443,socket.AF_INET);assert resolved;private=False\ntry:socket.gethostbyname('home-tunnel.karyo.test');private=True\nexcept socket.gaierror:pass\nassert not private;result=subprocess.run(['/usr/bin/curl','-4','--fail','--silent','--show-error','--max-time','15','https://api.ipify.org'],capture_output=True,text=True);assert result.returncode==0;ipaddress.IPv4Address(result.stdout.strip());print(json.dumps({'ordinary_external_dns':True,'home_private_name_hidden':True,'external_https_over_work':True}))"
                value_work=run(*user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect','--unit=karyo-qa-home-vpn-probe.service','--',
                                     '/usr/bin/python3','-I','-c',code))
                require(json.loads(value_work)['home_private_name_hidden'],'Work inherited Home DNS')
                print(value_work,flush=True)
            if mode=='locked-crash':
                wait_for(lambda:sessions.get(graph['login']['id']).locked,'Protected public Work did not lock')
            else:
                require(not switch(child.name,'karyoqa').get('starting',False),'Home recreated')
                require(not switch('karyoqa',child.name).get('starting',False),'Running Work recreated')
                current=journal()['profiles'][0]
                require(current['token']==record['token'] and current['provider']==record['provider'],'Existing switch replaced provider')
            if mode=='logout':
                run(*user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect','--unit=karyo-qa-public-vpn-logout.service','--','/usr/bin/labwc','--exit'))
            else:
                fd=os.pidfd_open(graph['pid'])
                try:
                    require(stat_start(graph['pid'])==graph['start'],'Captured compositor start changed')
                    exe=os.stat('/proc/'+str(graph['pid'])+'/exe');require([exe.st_dev,exe.st_ino]==graph['compositor'],'Captured compositor executable changed')
                    signal.pidfd_send_signal(fd,signal.SIGKILL)
                finally:os.close(fd)
            wait_for(lambda:not journal()['profiles'],'Public provider supervisor did not clean up')
            require(sessions.get(source.id)==source and snapshot()==original,'Original Home/network changed')
            with KernelConsoles() as consoles:require(consoles.state().active==source.vt,'Public provider did not return Home')
            print(json.dumps({'mode':mode,'cold_public_provider':True,'ordinary_status':value.get('daemonStatus'),
                              'connected':connected,'browser_pkce':pkce,'real_home_wireguard':homevpn,'home_wireguard_reconnect':reconnect,'state_reused':identity==state_identity,'existing_switch_unchanged':True,
                              'home_return':True,'cleanup':'complete'}),flush=True)
            with store.locked():store.save_settings(child.name,policy)
finally:
    require(not journal()['profiles'],'Retained public resources require review; child kept')
    with store.locked():
        require(store.settings(child.name)==policy,'QA policy changed; retain account')
        store.save_settings(child.name,original_policy)
    if keyfile is not None:
        require((keyfile.stat().st_dev,keyfile.stat().st_ino)==key_identity and keyfile.read_bytes()==key,'Lab key changed')
        keyfile.unlink()
    if state_identity is not None:
        state=Path('/var/lib/karyo/profile-vpn/1002/netbird')
        require((state.stat().st_dev,state.stat().st_ino)==state_identity and
                json.loads((state/'owner.json').read_bytes())['account']['name']==child.name,'Provider fixture identity changed')
        require(shutil.rmtree.avoids_symlink_attacks,'Native symlink-safe cleanup required')
        shutil.rmtree(state);state.parent.rmdir()
    run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
require({p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in original_accounts}==original_accounts,'Accounts/PAM changed')
require(snapshot()==original and sessions.get(source.id)==source and (run('/usr/sbin/ip','netns','list').split()[0]=='karyo-qa-home-wg' if homevpn else not run('/usr/sbin/ip','netns','list')),'Original host changed')
require(not run('/usr/bin/dpkg','-V','karyo-profile-accounts','karyo-profile-host','karyo-profile-runtime','karyo-profile-netbird-client') and not run('/usr/bin/dpkg','--audit'),'Package integrity failed')
print('PASS installed public VPN startup; original Home/network/accounts retained',flush=True)
