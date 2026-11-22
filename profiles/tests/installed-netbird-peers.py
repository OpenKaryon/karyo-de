#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host two-peer payload and private-NSS acceptance controller."""
import asyncio
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import select
import shutil
import socket
import stat
import subprocess
import sys
import time
import uuid

PORT = 43861
HOSTS = {'karyo-de-acceptance': 'debian', 'karyo-de-ubuntu-acceptance': 'ubuntu'}
assert socket.gethostname() in HOSTS
BASE = HOSTS[socket.gethostname()]


def address(value):
    parsed = ipaddress.IPv4Address(value)
    assert str(parsed) == value and parsed in ipaddress.IPv4Network('100.64.0.0/10')
    return value


def name(value):
    assert isinstance(value, str) and value.endswith('.netbird.selfhosted') and len(value) < 254
    assert all(c.isascii() and (c.isalnum() or c in '-.') for c in value)
    return value


def answer(data):
    assert 0 < len(data) <= 65536
    return b'karyo-work-peer:' + hashlib.sha256(data).digest()


async def server(ip):
    async def accept(reader, writer):
        try:
            data = await asyncio.wait_for(reader.readexactly(65536), 8)
            writer.write(answer(data)); await writer.drain()
        finally:
            writer.close(); await writer.wait_closed()
    class UDP(asyncio.DatagramProtocol):
        def connection_made(self, transport): self.transport = transport
        def datagram_received(self, data, peer): self.transport.sendto(answer(data), peer)
    tcp = await asyncio.start_server(accept, ip, PORT)
    transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(UDP, local_addr=(ip, PORT))
    print(json.dumps({'server_pid': os.getpid(), 'address': ip, 'port': PORT}), flush=True)
    try:
        async with tcp: await tcp.serve_forever()
    finally: transport.close()


def client(ip, fqdn, nonce, home=False):
    assert len(nonce) == 64 and bytes.fromhex(nonce)
    result = {'uid': os.getuid(), 'net': os.readlink('/proc/self/ns/net'), 'dns_addresses': []}
    try:
        result['dns_addresses'] = sorted({r[4][0] for r in socket.getaddrinfo(fqdn, PORT, socket.AF_INET)})
    except socket.gaierror as error: result['dns_errno'] = error.errno
    if home:
        result['provider_endpoint_inherited'] = 'KARYO_NETBIRD_ENDPOINT' in os.environ
        print(json.dumps(result)); return
    payload = (bytes.fromhex(nonce) * 2048)[:65536]
    for label, host in [('ip', ip), ('dns', fqdn)]:
        if label == 'dns' and ip not in result['dns_addresses']: continue
        try:
            with socket.create_connection((host, PORT), timeout=8) as sock:
                sock.sendall(payload); sock.shutdown(socket.SHUT_WR)
                response = b''
                while len(response) < len(answer(payload)):
                    block = sock.recv(4096)
                    if not block: break
                    response += block
                result['tcp_' + label] = response == answer(payload)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(8); sock.sendto(payload[:1024], (host, PORT))
                response, peer = sock.recvfrom(4096)
                result['udp_' + label] = response == answer(payload[:1024]) and peer == (ip, PORT)
        except OSError as error: result[label + '_errno'] = error.errno
    print(json.dumps(result))


def exit_client(nonce):
    assert len(nonce) == 64 and bytes.fromhex(nonce)
    payload = bytes.fromhex(nonce) * 2048
    result = {'uid': os.getuid(), 'net': os.readlink('/proc/self/ns/net')}
    try:
        with socket.create_connection(('198.18.0.11', 43862), timeout=8) as sock:
            sock.sendall(payload); sock.shutdown(socket.SHUT_WR); raw = b''
            while len(raw) < 4096:
                block = sock.recv(4096)
                if not block: break
                raw += block
            value = json.loads(raw)
            result['tcp'] = value['digest'] == hashlib.sha256(payload).hexdigest()
            result['tcp_source'] = value['source']; result['tcp_host'] = value['host']
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(8); sock.sendto(payload[:1024], ('198.18.0.11', 43862))
            raw, peer = sock.recvfrom(4096); value = json.loads(raw)
            result['udp'] = value['digest'] == hashlib.sha256(payload[:1024]).hexdigest() and peer == ('198.18.0.11', 43862)
            result['udp_source'] = value['source']; result['udp_host'] = value['host']
    except OSError as error: result['errno'] = error.errno
    print(json.dumps(result))


if sys.argv[1:2] == ['--exit-client']:
    assert os.getuid() == os.geteuid() == 1002 and len(sys.argv) == 3
    exit_client(sys.argv[2]); raise SystemExit(0)
if sys.argv[1:2] == ['--server']:
    assert os.getuid() == os.geteuid() == 1002 and len(sys.argv) == 3
    asyncio.run(server(address(sys.argv[2]))); raise SystemExit(0)
if sys.argv[1:2] in (['--client'], ['--home']):
    home = sys.argv[1] == '--home'
    assert os.getuid() == os.geteuid() == (1001 if home else 1002) and len(sys.argv) == 5
    client(address(sys.argv[2]), name(sys.argv[3]), sys.argv[4], home); raise SystemExit(0)
assert os.getuid() == os.geteuid() == 0 and not sys.argv[1:]
sys.dont_write_bytecode = True
sys.path.insert(0, '/usr/libexec/karyo/profiles')
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import discover_dns
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS, process_gone
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.journal_netbird import JournalNetBird
from karyo_profiles.journal_session import JournalSession, stat_start
from karyo_profiles.logind import Sessions
from karyo_profiles.storage import ConfigStore
from karyo_profiles.switching import ExistingSwitcher
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.user_manager import ManagerPending

root = Path('/var/lib/karyo-qa-netbird-peers')
assert not os.path.lexists(root) and not os.path.lexists('/usr/bin/netbird')
assert not os.path.lexists('/var/lib/karyo/profile-vpn/1002')
assert Path('/proc/sys/net/ipv4/ip_forward').read_text().strip() == '0'
store, accounts, sessions = ConfigStore(), LocalAccounts(), Sessions()
source = next(s for s in sessions.all() if s.uid == 1001 and s.name == 'karyoqa' and s.active)


def run(*args): return subprocess.check_output(args, text=True, timeout=20).strip()


def host_snapshot():
    return {key: run(*args) for key, args in {
        'routes': ('/usr/sbin/ip', '-4', '-j', '-N', 'route', 'show', 'table', 'all'),
        'rules': ('/usr/sbin/ip', '-4', '-j', '-N', 'rule', 'show'),
        'nft': ('/usr/sbin/nft', '-j', 'list', 'ruleset')}.items()} | {
        'resolver_sha': hashlib.sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest(),
        'resolver': os.readlink('/etc/resolv.conf')}


assert not run('/usr/sbin/ip', 'netns', 'list')
with RuntimeJournal() as journal: assert not journal.snapshot()['profiles']
os.mkdir(root, 0o700)
original_host = host_snapshot()
original_conf = {str(p): p.read_text() for p in Path('/proc/sys/net/ipv4/conf').glob('*/*')}
original_accounts = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [Path('/etc/passwd'), Path('/etc/shadow'), Path('/etc/group'), Path('/etc/gshadow'), *Path('/etc/pam.d').glob('common-*')]}
package = Path('/home/qaadmin/netbird-client-input/netbird_0.82.1_linux_amd64.deb')
assert hashlib.sha256(package.read_bytes()).hexdigest() == '4adff2bbc1529942e914c181badfc17585c78f0532ccaf6c000f5c9300d9a632'
subprocess.run(['/usr/bin/dpkg-deb', '-x', str(package), str(root/'input')], check=True)
binary = root/'input/usr/bin/netbird'; binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
subprocess.run(['/usr/bin/install', '-o', 'root', '-g', 'root', '-m', '755', str(binary), '/usr/bin/netbird'], check=True)
run('/usr/local/libexec/karyo-qa-two-sessions', 'prepare')
child = accounts.account('kq-switch-work'); assert child.uid == 1002
admission = Path('/var/lib/karyo-qa-netbird-admission/setup-key'); info = admission.lstat()
assert stat.S_ISREG(info.st_mode) and info.st_uid == info.st_gid == 0 and info.st_mode&0o777 == 0o600 and info.st_nlink == 1 and info.st_size < 65
key = admission.read_bytes(); uuid.UUID(key.decode().strip())
keyfile = Path(child.home)/'.karyo-qa-peer-setup-key'
fd = os.open(keyfile, os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW, 0o600)
try: os.fchown(fd, 1002, 1002); os.write(fd, key); os.fsync(fd)
finally: os.close(fd)
key_inode = (keyfile.stat().st_dev, keyfile.stat().st_ino)
operations = {}


def user(*args, home=False):
    uid, account = (1001, 'karyoqa') if home else (1002, child.name)
    return ['/usr/sbin/runuser', '-u', account, '--', '/usr/bin/env', 'XDG_RUNTIME_DIR=/run/user/'+str(uid),
            'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus', *args]


def managed(label, *args, home=False, timeout=65):
    number = operations.get(label, 0); operations[label] = number+1
    result = subprocess.run(user('/usr/bin/systemd-run', '--user', '--wait', '--pipe', '--collect',
        '--unit=karyo-qa-peer-'+label+'-'+str(number)+'.service', '--', *args, home=home),
        capture_output=True, text=True, timeout=timeout)
    fd = os.open(root/(label+'-'+str(number)+'.log'), os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0o600)
    try: os.write(fd, (result.stdout+result.stderr).encode())
    finally: os.close(fd)
    assert result.returncode == 0, 'Managed '+label+' failed'
    return result.stdout


Path('/proc/sys/net/ipv4/ip_forward').write_text('1\n')
with RuntimeJournal() as journal:
    with store.locked():
        network = JournalNetwork.start(journal, store, child, source); native = network.network
        physical = discover_underlay(native.command)
        forwarding = JournalForwarding.start(journal, network, physical)
        dns = JournalDNS.start(journal, network, discover_dns(physical, native.command))
        provider = JournalNetBird.start(journal, dns); state_identity = provider.scope.state.identity
        graph = JournalSession.start(journal, dns); provider.release_handles(); graph.enter(accounts.account('karyoqa'))
    managed('enroll', '/usr/bin/karyo-netbird-cli', 'up', '--management-url', 'http://192.168.230.163:18443',
        '--setup-key-file', str(keyfile), '--hostname', 'karyo-peer-'+BASE)
    status = json.loads(managed('status', '/usr/bin/karyo-netbird-cli', 'status', '--json'))
    assert status.get('daemonStatus') == 'Connected'
    ip, fqdn = address(status['netbirdIp'].split('/')[0]), name(status['fqdn'])
    server_name = 'karyo-qa-peer-server-'+graph.token+'.service'
    server_process = subprocess.Popen(user('/usr/bin/systemd-run', '--user', '--wait', '--pipe', '--collect',
        '--unit='+server_name, '--', '/usr/bin/python3', '-I', __file__, '--server', ip),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert select.select([server_process.stdout], [], [], 15)[0], 'Peer server did not start'
    packet = server_process.stdout.readline(1025)
    assert packet, 'Peer server exited before readiness'
    server_ready = json.loads(packet); assert server_ready['address'] == ip and server_ready['port'] == PORT
    def server_info():
        return dict(r.split('=', 1) for r in run(*user('/usr/bin/systemctl', '--user', 'show', server_name,
            '-p', 'InvocationID', '-p', 'MainPID', '-p', 'ActiveState')).splitlines())
    captured_server = server_info()
    assert captured_server['ActiveState'] == 'active' and int(captured_server['MainPID']) == server_ready['server_pid']
    server_start = stat_start(server_ready['server_pid'])
    with store.locked(): graph.verify_consumers()
    print(json.dumps({'ready': True, 'base': BASE, 'ip': ip, 'fqdn': fqdn, 'port': PORT, 'guest': native.spec.guest}), flush=True)
    observations = []
    while True:
        assert select.select([sys.stdin], [], [], 360)[0], 'Peer controller input timed out'
        line = sys.stdin.buffer.readline(8193); assert line and len(line) <= 8192
        request = json.loads(line)
        if request == {'operation': 'close'}: break
        if set(request) == {'operation', 'nonce'} and request['operation'] == 'exit':
            with store.locked(): graph.verify_consumers(); graph.provider.verify()
            deadline = time.monotonic()+35
            while 'karyo-qa-exit' not in managed('networks', '/usr/bin/karyo-netbird-cli', 'networks', 'list'):
                assert time.monotonic() < deadline, 'Published exit route was not delivered'; time.sleep(.2)
            managed('select', '/usr/bin/karyo-netbird-cli', 'networks', 'select', 'karyo-qa-exit')
            before = json.loads(native._ip('-s', '-j', 'link', 'show', 'dev', 'wt0', inside=True))[0]
            route = json.loads(native._ip('-4', '-j', 'route', 'get', '198.18.0.11', inside=True))
            result = json.loads(managed('exit', '/usr/bin/python3', '-I', __file__, '--exit-client', request['nonce']))
            after = json.loads(native._ip('-s', '-j', 'link', 'show', 'dev', 'wt0', inside=True))[0]
            observation = {'base': BASE, 'route': route, 'ordinary': result, 'before': before, 'after': after,
                'rules': json.loads(native._ip('-4', '-j', '-N', 'rule', 'show', inside=True))}
            observations.append(observation); print(json.dumps({'exit_observation': observation}), flush=True)
            continue
        assert set(request) == {'operation', 'ip', 'fqdn', 'nonce'} and request['operation'] == 'peer'
        peer_ip, peer_name = address(request['ip']), name(request['fqdn'])
        with store.locked(): graph.verify_consumers(); graph.provider.verify()
        before = json.loads(native._ip('-s', '-j', 'link', 'show', 'dev', 'wt0', inside=True))[0]
        route = json.loads(native._ip('-4', '-j', 'route', 'get', peer_ip, inside=True))
        result = json.loads(managed('payload', '/usr/bin/python3', '-I', __file__, '--client', peer_ip, peer_name, request['nonce']))
        home = json.loads(managed('home', '/usr/bin/python3', '-I', __file__, '--home', peer_ip, peer_name, request['nonce'], home=True))
        after = json.loads(native._ip('-s', '-j', 'link', 'show', 'dev', 'wt0', inside=True))[0]
        observation = {'base': BASE, 'peer_ip': peer_ip, 'peer_fqdn': peer_name, 'route': route,
            'ordinary': result, 'home': home, 'before': before, 'after': after}
        observations.append(observation)
        print(json.dumps({'observation': observation}), flush=True)
    assert server_info() == captured_server and stat_start(server_ready['server_pid']) == server_start
    run(*user('/usr/bin/systemctl', '--user', 'stop', server_name))
    server_process.communicate(timeout=15)
    assert process_gone(server_ready['server_pid'], server_start)
    ExistingSwitcher(store).switch(child, 'karyoqa')
    deadline = time.monotonic()+35
    while True:
        try:
            with store.locked(): graph.close()
            break
        except ManagerPending:
            assert time.monotonic() < deadline; time.sleep(.1)
    with store.locked(): graph.provider.close(); dns.close(); forwarding.close(); network.close()
    graph.release_handles(); assert not journal.snapshot()['profiles']
Path('/proc/sys/net/ipv4/ip_forward').write_text('0\n')
for filename, data in original_conf.items():
    if Path(filename).read_text() != data: Path(filename).write_text(data)
assert (keyfile.stat().st_dev, keyfile.stat().st_ino) == key_inode and keyfile.read_bytes() == key; keyfile.unlink()
state = Path('/var/lib/karyo/profile-vpn/1002/netbird')
assert (state.stat().st_dev, state.stat().st_ino) == state_identity
assert json.loads((state/'owner.json').read_text())['account']['name'] == child.name
assert shutil.rmtree.avoids_symlink_attacks; shutil.rmtree(state); state.parent.rmdir()
run('/usr/local/libexec/karyo-qa-two-sessions', 'cleanup')
assert {filename: hashlib.sha256(Path(filename).read_bytes()).hexdigest() for filename in original_accounts} == original_accounts
assert sessions.get(source.id) == source and not run('/usr/sbin/ip', 'netns', 'list')
assert hashlib.sha256(Path('/usr/bin/netbird').read_bytes()).hexdigest() == binary_sha; Path('/usr/bin/netbird').unlink()
assert host_snapshot() == original_host
assert not run('/usr/bin/dpkg', '-V', 'karyo-profile-accounts', 'karyo-profile-host', 'karyo-profile-runtime', 'karyo-profile-netbird-client')
assert not run('/usr/bin/dpkg', '--audit')
shutil.rmtree(root)
print(json.dumps({'closed': True, 'base': BASE, 'observations': len(observations), 'host_restored': True}), flush=True)
