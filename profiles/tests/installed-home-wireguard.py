#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real disposable-host Home WireGuard/DNS fixture with captured teardown."""
import asyncio,hashlib,ipaddress,json,os,secrets,socket,stat,struct,subprocess,sys,time,uuid
from pathlib import Path
HOSTS={'karyo-de-acceptance','karyo-de-ubuntu-acceptance'}
if socket.gethostname() not in HOSTS:raise SystemExit('Fixed disposable hosts only')
ROOT=Path('/var/lib/karyo-qa-home-wireguard')
KEYS=Path('/etc/wireguard/karyo-qa-home')
NS='karyo-qa-home-wg';HOST='kqhw-a';PEER='kqhw-b';WG='kq-home-wg';REMOTE='kq-peer-wg'
UNIT='karyo-qa-home-wireguard.service';VIP='198.18.0.77';NAME='home-tunnel.karyo.test';PORT=43867


def require(value,message):
    if not value:raise RuntimeError(message)


def run(*args,input=None):
    return subprocess.check_output(args,input=input,text=True,timeout=25).strip()


def dns(data):
    if not 12<len(data)<=512 or data[2:4]!=bytes.fromhex('0100') or data[4:6]!=b'\x00\x01':return None
    offset=12;labels=[]
    while offset<len(data) and data[offset]:
        count=data[offset];offset+=1
        if count>63 or offset+count>len(data):return None
        labels.append(data[offset:offset+count].decode('ascii'));offset+=count
    question=data[12:offset+5]
    good='.'.join(labels)==NAME and data[offset+1:offset+5]==b'\x00\x01\x00\x01'
    header=data[:2]+(b'\x81\x80' if good else b'\x81\x83')+b'\x00\x01'+(b'\x00\x01' if good else b'\x00\x00')+b'\x00\x00\x00\x00'
    answer=b'\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x00\x00\x04'+socket.inet_aton(VIP)
    return header+question+(answer if good else b'')


async def server():
    class DNS(asyncio.DatagramProtocol):
        def connection_made(self,transport):self.transport=transport
        def datagram_received(self,data,peer):
            try:value=dns(data)
            except (UnicodeError,IndexError):return
            if value:self.transport.sendto(value,peer)
    async def accept(reader,writer):
        try:
            data=await asyncio.wait_for(reader.readexactly(4096),8)
            writer.write(json.dumps({'digest':hashlib.sha256(data).hexdigest(),'source':writer.get_extra_info('peername')[0]}).encode())
            await writer.drain()
        finally:writer.close();await writer.wait_closed()
    transport,_=await asyncio.get_running_loop().create_datagram_endpoint(DNS,local_addr=(VIP,53))
    tcp=await asyncio.start_server(accept,VIP,PORT)
    try:
        async with tcp:await tcp.serve_forever()
    finally:transport.close()


if sys.argv[1:]==['--server']:
    require(os.getuid()==os.geteuid()==1000,'Ordinary QA server only')
    asyncio.run(server());raise SystemExit(0)
require(os.getuid()==os.geteuid()==0,'Root fixture controller only')
sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.dns import discover_dns
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.services import NativeProcess


def snapshot():
    return {name:run(*args) for name,args in {
      'routes':('/usr/sbin/ip','-4','-j','-N','route','show','table','all'),
      'rules':('/usr/sbin/ip','-4','-j','-N','rule','show'),
      'nft':('/usr/sbin/nft','-j','list','ruleset')}.items()}|{
        'resolver':os.readlink('/etc/resolv.conf'),'resolver_sha':hashlib.sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest(),
        'forward':Path('/proc/sys/net/ipv4/ip_forward').read_text()}


def save():
    (ROOT/'receipt.json').write_bytes((json.dumps(record)+'\n').encode())


def link(name,namespace=False):
    args=['/usr/sbin/ip',*(['-n',NS] if namespace else []),'-j','-d','link','show','dev',name]
    rows=json.loads(run(*args));require(len(rows)==1,'Ambiguous link');return rows[0]


def capture():
    info=ROOT.lstat();require(stat.S_ISDIR(info.st_mode) and info.st_uid==0 and stat.S_IMODE(info.st_mode)==0o700,'Unsafe fixture directory')
    path=ROOT/'receipt.json';info=path.lstat();require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and info.st_nlink==1 and stat.S_IMODE(info.st_mode)==0o600,'Unsafe fixture receipt')
    value=json.loads(path.read_bytes());require(value['phase'] in ('live','disconnected') and [ROOT.stat().st_dev,ROOT.stat().st_ino]==value['directory'],'Incomplete fixture')
    for name,namespace in ((HOST,False),(WG,False),(PEER,True),(REMOTE,True)):
        if name==WG and value['phase']=='disconnected':continue
        row=link(name,namespace);require([row['ifindex'],row['ifalias']]==value['links'][name],'Captured link changed')
    ns=os.stat('/run/netns/'+NS);require([ns.st_dev,ns.st_ino]==value['namespace'],'Namespace changed')
    if value['phase']=='live':
        require(run('/usr/bin/wg','show',WG,'public-key')==value['public']['host'],'Home WG key changed')
    else:
        require(all(row['ifname']!=WG for row in json.loads(run('/usr/sbin/ip','-j','link','show'))),'Disconnected WG link remains')
        require(not any(row.get('priority')==90 for row in json.loads(run('/usr/sbin/ip','-4','-j','-N','rule','show'))),'Disconnected rule remains')
        require(not any(str(row.get('table'))=='54000' for row in json.loads(run('/usr/sbin/ip','-4','-j','-N','route','show','table','all'))),'Disconnected route remains')
    require(run('/usr/sbin/ip','netns','exec',NS,'/usr/bin/wg','show',REMOTE,'public-key')==value['public']['peer'],'Peer WG key changed')
    unit=dict(line.split('=',1) for line in run('/usr/bin/systemctl','show',UNIT,'-p','MainPID','-p','InvocationID','-p','ControlGroup','-p','ActiveState').splitlines())
    require(unit==value['unit'] and unit['ActiveState']=='active','Captured server changed')
    require(NativeProcess.read(int(unit['MainPID']),tuple(value['exe']),1000,unit['ControlGroup'],tuple(value['namespace']))==value['start'],'Server process changed')
    return value


def probe():
    code="import hashlib,json,socket;name=socket.gethostbyname('home-tunnel.karyo.test');assert name=='198.18.0.77';payload=(b'Karyo actual Home WireGuard'+bytes(4096))[:4096];s=socket.create_connection((name,43867),timeout=8);s.sendall(payload);value=json.loads(s.recv(4096));assert value['digest']==hashlib.sha256(payload).hexdigest() and value['source']=='10.240.254.1';print(json.dumps({'dns':name,'tcp_source':value['source'],'digest':True}))"
    value=json.loads(run('/usr/sbin/runuser','-u','karyoqa','--','/usr/bin/python3','-I','-c',code))
    handshake=run('/usr/bin/wg','show',WG,'latest-handshakes').split();require(len(handshake)==2 and int(handshake[1])>0,'No real WG handshake')
    counters=run('/usr/bin/wg','show',WG,'transfer').split();require(len(counters)==3 and min(map(int,counters[1:]))>0,'WG carried no bidirectional payload')
    return value|{'kernel_handshake':True,'encrypted_payload_bytes':[int(v) for v in counters[1:]]}


action=sys.argv[1:]
if action==['prepare']:
    require(not os.path.lexists(ROOT) and not os.path.lexists(KEYS),'Prior Home VPN requires review')
    require(not run('/usr/sbin/ip','netns','list'),'Prior namespace requires review')
    require(not json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles'],'Prior profile requires review')
    require(all(r.get('ifname') not in {HOST,PEER,WG,REMOTE} for r in json.loads(run('/usr/sbin/ip','-j','link','show'))),'Prior links')
    require(not any(r.get('priority') in (80,81,90) or str(r.get('table'))=='54000' for r in json.loads(run('/usr/sbin/ip','-4','-j','-N','rule','show'))),'Reserved rules in use')
    require(not any(str(r.get('table'))=='54000' for r in json.loads(run('/usr/sbin/ip','-4','-j','-N','route','show','table','all'))),'Reserved table in use')
    require(run('/usr/bin/systemctl','show',UNIT,'-p','LoadState','--value')=='not-found','Prior server unit')
    original=snapshot();seed=discover_dns(discover_underlay());os.mkdir(ROOT,0o700);os.mkdir(KEYS,0o700)
    record={'phase':'creating','directory':[ROOT.stat().st_dev,ROOT.stat().st_ino],'before':original,'physical_dns':list(seed.servers),'key_directory':[KEYS.stat().st_dev,KEYS.stat().st_ino],'links':{},'public':{}}
    fd=os.open(ROOT/'receipt.json',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd);save()
    for role in ('host','peer'):
        private=run('/usr/bin/wg','genkey')+'\n';public=run('/usr/bin/wg','pubkey',input=private)
        fd=os.open(KEYS/(role+'.key'),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
        try:os.write(fd,private.encode())
        finally:os.close(fd)
        record['public'][role]=public;save()
    run('/usr/sbin/ip','netns','add',NS)
    ns=os.stat('/run/netns/'+NS);record['namespace']=[ns.st_dev,ns.st_ino];save()
    run('/usr/sbin/ip','link','add',HOST,'type','veth','peer','name',PEER)
    run('/usr/sbin/ip','link','set',PEER,'netns',NS)
    for name,namespace in ((HOST,False),(PEER,True)):
        args=['/usr/sbin/ip',*(['-n',NS] if namespace else [])]
        alias='Karyo QA Home WG '+uuid.uuid4().hex;run(*args,'link','set',name,'alias',alias)
        row=link(name,namespace);record['links'][name]=[row['ifindex'],alias];save()
        run(*args,'address','add',('172.31.254.2' if namespace else '172.31.254.1')+'/30','dev',name)
        run(*args,'link','set',name,'up')
    run('/usr/sbin/ip','-n',NS,'link','set','lo','up');run('/usr/sbin/ip','-n',NS,'address','add',VIP+'/32','dev','lo')
    for name,namespace in ((WG,False),(REMOTE,True)):
        args=['/usr/sbin/ip',*(['-n',NS] if namespace else [])]
        run(*args,'link','add',name,'type','wireguard')
        alias='Karyo QA Home WG '+uuid.uuid4().hex;run(*args,'link','set',name,'alias',alias)
        row=link(name,namespace);record['links'][name]=[row['ifindex'],alias];save()
        role='peer' if namespace else 'host';other='host' if namespace else 'peer'
        wgargs=['/usr/sbin/ip','netns','exec',NS] if namespace else []
        run(*wgargs,'/usr/bin/wg','set',name,'private-key',str(KEYS/(role+'.key')),'listen-port','51867' if namespace else '51868',
            'peer',record['public'][other],'allowed-ips','10.240.254.1/32' if namespace else '0.0.0.0/0',
            'endpoint','172.31.254.1:51868' if namespace else '172.31.254.2:51867')
        run(*args,'address','add',('10.240.254.2' if namespace else '10.240.254.1')+'/30','dev',name);run(*args,'link','set',name,'up')
    for pref,destination in ((80,'192.168.230.0/24'),(81,'172.31.254.0/30')):
        run('/usr/sbin/ip','-4','rule','add','priority',str(pref),'to',destination,'lookup','main')
    run('/usr/sbin/ip','-4','route','add','default','dev',WG,'table','54000')
    run('/usr/sbin/ip','-4','rule','add','priority','90','lookup','54000')
    run('/usr/bin/resolvectl','dns',WG,VIP);run('/usr/bin/resolvectl','domain',WG,'~.')
    run('/usr/bin/systemd-run','--unit='+UNIT,'--collect','--property=Type=exec','--property=User=qaadmin','--property=Group=qaadmin',
        '--property=NetworkNamespacePath=/run/netns/'+NS,'--property=NoNewPrivileges=yes','--property=AmbientCapabilities=CAP_NET_BIND_SERVICE',
        '--property=CapabilityBoundingSet=CAP_NET_BIND_SERVICE','--property=KillMode=control-group','--property=TimeoutStopSec=10s',
        '--','/usr/bin/python3','-I','/usr/local/libexec/karyo-qa-home-wireguard','--server')
    record['unit']=dict(line.split('=',1) for line in run('/usr/bin/systemctl','show',UNIT,'-p','MainPID','-p','InvocationID','-p','ControlGroup','-p','ActiveState').splitlines())
    info=os.stat('/usr/bin/python3');record['exe']=[info.st_dev,info.st_ino]
    record['start']=NativeProcess.read(int(record['unit']['MainPID']),tuple(record['exe']),1000,record['unit']['ControlGroup'],tuple(record['namespace']))
    record['phase']='live';save();capture()
    print(json.dumps({'home_wireguard_ready':True,'physical_dns':record['physical_dns']}),flush=True)
elif action==['disconnect']:
    record=capture();require(record['phase']=='live','Home VPN is not live')
    require(json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles'],'Live Work required')
    rules=[row for row in json.loads(run('/usr/sbin/ip','-4','-j','-N','rule','show')) if row.get('priority')==90]
    require(len(rules)==1 and set(rules[0])=={'priority','src','table'} and rules[0]['src']=='all' and str(rules[0]['table'])=='54000','VPN policy changed')
    routes=[row for row in json.loads(run('/usr/sbin/ip','-4','-j','-N','route','show','table','all')) if str(row.get('table'))=='54000']
    require(len(routes)==1 and routes[0].get('dev')==WG and routes[0].get('dst')=='default','VPN route changed')
    record['phase']='disconnecting';save()
    run('/usr/bin/resolvectl','revert',WG)
    run('/usr/sbin/ip','-4','rule','delete','priority','90','lookup','54000')
    run('/usr/sbin/ip','link','delete',WG)
    record['phase']='disconnected';save();capture()
    print(json.dumps({'home_wireguard_disconnected':True,'link_absent':True,'vpn_default_absent':True}),flush=True)
elif action==['probe-disconnected']:
    record=capture();require(record['phase']=='disconnected','Expected disconnected Home')
    code="import json,socket;assert socket.getaddrinfo('example.com',443,socket.AF_INET);hidden=False\ntry:socket.gethostbyname('home-tunnel.karyo.test')\nexcept socket.gaierror:hidden=True\nassert hidden;print(json.dumps({'physical_dns':True,'home_private_name_absent':True}))"
    print(run('/usr/sbin/runuser','-u','karyoqa','--','/usr/bin/python3','-I','-c',code),flush=True)
elif action==['reconnect']:
    record=capture();require(record['phase']=='disconnected','Expected disconnected Home')
    require([KEYS.stat().st_dev,KEYS.stat().st_ino]==record['key_directory'],'Key directory changed')
    key=KEYS/'host.key';info=key.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and info.st_nlink==1 and stat.S_IMODE(info.st_mode)==0o600,'Private key changed')
    require(run('/usr/bin/wg','pubkey',input=key.read_text())==record['public']['host'],'Private key changed')
    record['phase']='reconnecting';save();previous=record['links'][WG]
    run('/usr/sbin/ip','link','add',WG,'type','wireguard')
    alias='Karyo QA Home WG '+uuid.uuid4().hex;run('/usr/sbin/ip','link','set',WG,'alias',alias)
    row=link(WG);record['links'][WG]=[row['ifindex'],alias];save()
    run('/usr/bin/wg','set',WG,'private-key',str(key),'listen-port','51868','peer',record['public']['peer'],
        'allowed-ips','0.0.0.0/0','endpoint','172.31.254.2:51867')
    run('/usr/sbin/ip','address','add','10.240.254.1/30','dev',WG);run('/usr/sbin/ip','link','set',WG,'up')
    run('/usr/sbin/ip','-4','route','add','default','dev',WG,'table','54000')
    run('/usr/sbin/ip','-4','rule','add','priority','90','lookup','54000')
    run('/usr/bin/resolvectl','dns',WG,VIP);run('/usr/bin/resolvectl','domain',WG,'~.')
    record['phase']='live';save();capture()
    require(record['links'][WG]!=previous,'The tunnel link was not replaced')
    print(json.dumps({'home_wireguard_reconnected':True,'new_link_identity':True}),flush=True)
elif action==['probe']:
    record=capture();require(record['phase']=='live','Home VPN is disconnected');print(json.dumps(probe()),flush=True)
elif action==['cleanup']:
    record=capture();require(record['phase']=='live','Reconnect captured Home before cleanup')
    require(not json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles'],'Close Work first')
    rules=((80,'192.168.230.0/24','254'),(81,'172.31.254.0/30','254'),(90,None,'54000'))
    for pref,destination,table in rules:
        rows=[r for r in json.loads(run('/usr/sbin/ip','-4','-j','-N','rule','show')) if r.get('priority')==pref]
        require(len(rows)==1,'Rule inventory changed')
        row=rows[0];actual=str(ipaddress.ip_network(row['dst']+'/'+str(row['dstlen']))) if 'dst' in row else None
        require(row.get('src')=='all' and actual==destination and str(row['table'])==table,'Rule changed')
    require([KEYS.stat().st_dev,KEYS.stat().st_ino]==record['key_directory'],'Key directory changed')
    run('/usr/bin/systemctl','stop',UNIT)
    run('/usr/bin/resolvectl','revert',WG)
    for pref,destination,table in rules:
        run('/usr/sbin/ip','-4','rule','delete','priority',str(pref),*(['to',destination] if destination else []),'lookup',table)
    for name in (WG,HOST):
        row=link(name);require([row['ifindex'],row['ifalias']]==record['links'][name],'Host fixture link changed')
        run('/usr/sbin/ip','link','delete',name)
    ns=os.stat('/run/netns/'+NS);require([ns.st_dev,ns.st_ino]==record['namespace'],'Peer namespace changed')
    run('/usr/sbin/ip','netns','delete',NS)
    require(snapshot()==record['before'],'Original Home host network not restored')
    require(set(p.name for p in ROOT.iterdir())=={'receipt.json'} and set(p.name for p in KEYS.iterdir())=={'host.key','peer.key'},'Unknown fixture files')
    for role in ('host','peer'):
        path=KEYS/(role+'.key');info=path.lstat();require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and info.st_nlink==1 and stat.S_IMODE(info.st_mode)==0o600,'Private key changed')
        require(run('/usr/bin/wg','pubkey',input=path.read_text())==record['public'][role],'Key changed');path.unlink()
    KEYS.rmdir();(ROOT/'receipt.json').unlink();ROOT.rmdir()
    print('PASS exact real Home WG fixture removed; original route/rule/nft/resolver/forwarding snapshot restored',flush=True)
else:raise SystemExit('Use prepare, probe, disconnect, probe-disconnected, reconnect or cleanup')
