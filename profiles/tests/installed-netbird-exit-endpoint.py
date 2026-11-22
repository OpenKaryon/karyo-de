#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Captured external endpoint for fixed disposable-host exit-node forwarding QA."""
import asyncio
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import stat
import subprocess
import sys
import time

ROOT = Path('/var/lib/karyo-qa-netbird-exit')
INTERFACE = 'kq-exit-probe'
ADDRESS, PORT = '198.18.0.11', 43862
UNIT = 'karyo-qa-netbird-exit.service'
assert socket.gethostname() in {'karyo-de-acceptance', 'karyo-de-ubuntu-acceptance'}


def response(data, peer):
    assert 0 < len(data) <= 65536
    return (json.dumps({'digest': hashlib.sha256(data).hexdigest(), 'source': peer[0], 'host': socket.gethostname()})+'\n').encode()


async def serve():
    async def accept(reader, writer):
        try:
            data = await asyncio.wait_for(reader.readexactly(65536), 8)
            writer.write(response(data, writer.get_extra_info('peername'))); await writer.drain()
        finally: writer.close(); await writer.wait_closed()
    class UDP(asyncio.DatagramProtocol):
        def connection_made(self, transport): self.transport = transport
        def datagram_received(self, data, peer):
            if 0 < len(data) <= 65536: self.transport.sendto(response(data, peer), peer)
    tcp = await asyncio.start_server(accept, ADDRESS, PORT)
    transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(UDP, local_addr=(ADDRESS, PORT))
    fd = os.open(ROOT/'server/ready.json', os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW, 0o600)
    try: os.write(fd, json.dumps({'pid': os.getpid(), 'address': ADDRESS, 'port': PORT}).encode())
    finally: os.close(fd)
    try:
        async with tcp: await tcp.serve_forever()
    finally: transport.close()


if sys.argv[1:] == ['--server']:
    assert os.getuid() == os.geteuid() == 1000
    asyncio.run(serve()); raise SystemExit(0)
assert os.getuid() == os.geteuid() == 0
sys.dont_write_bytecode = True; sys.path.insert(0, '/usr/libexec/karyo/profiles')
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import process_gone
from karyo_profiles.services import NativeProcess, host_controller
host_controller()


def run(*args): return subprocess.check_output(args, text=True, timeout=20).strip()


def idle():
    assert Path('/proc/sys/net/ipv4/ip_forward').read_text().strip() == '0'
    assert not run('/usr/sbin/ip', 'netns', 'list')
    with RuntimeJournal() as journal: assert not journal.snapshot()['profiles']


def snapshot():
    return {key: run(*args) for key, args in {
        'routes': ('/usr/sbin/ip', '-4', '-j', '-N', 'route', 'show', 'table', 'all'),
        'rules': ('/usr/sbin/ip', '-4', '-j', '-N', 'rule', 'show'),
        'nft': ('/usr/sbin/nft', '-j', 'list', 'ruleset')}.items()} | {
        'resolver': os.readlink('/etc/resolv.conf'), 'resolver_sha': hashlib.sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest()}


def unit():
    return dict(row.split('=', 1) for row in run('/usr/bin/systemctl', 'show', UNIT,
        '-p', 'LoadState', '-p', 'ActiveState', '-p', 'MainPID', '-p', 'InvocationID', '-p', 'ControlGroup').splitlines())


def link():
    rows = json.loads(run('/usr/sbin/ip', '-j', '-d', 'link', 'show', 'dev', INTERFACE)); assert len(rows) == 1
    return rows[0]


def capture():
    info = ROOT.lstat(); assert stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and info.st_gid == 1000 and stat.S_IMODE(info.st_mode) == 0o750
    info = (ROOT/'receipt.json').lstat(); assert stat.S_ISREG(info.st_mode) and info.st_uid == info.st_gid == 0 and info.st_nlink == 1 and stat.S_IMODE(info.st_mode) == 0o600
    value = json.loads((ROOT/'receipt.json').read_text())
    assert value['pending'] is None and [ROOT.stat().st_dev, ROOT.stat().st_ino] == value['directory']
    ns = os.stat('/proc/1/ns/net'); assert [ns.st_dev, ns.st_ino] == value['network']
    row = link(); assert row['ifindex'] == value['index'] and row['linkinfo']['info_kind'] == 'dummy' and row['ifalias'] == value['alias']
    addresses = json.loads(run('/usr/sbin/ip', '-j', '-4', 'address', 'show', 'dev', INTERFACE))[0]['addr_info']
    assert [(r['local'], r['prefixlen']) for r in addresses] == [(ADDRESS, 32)]
    return value


def live(value):
    row = unit(); assert row == value['service'] and row['ActiveState'] == 'active'
    assert NativeProcess.read(int(row['MainPID']), tuple(value['exe']), 1000, row['ControlGroup'], tuple(value['network'])) == value['start']


def prepare():
    idle(); assert not os.path.lexists(ROOT) and unit()['LoadState'] == 'not-found'
    assert not any(r['ifname'] == INTERFACE for r in json.loads(run('/usr/sbin/ip', '-j', 'link', 'show')))
    for r in json.loads(run('/usr/sbin/ip', '-4', '-j', 'route', 'show', 'table', 'all')):
        assert r['dst'] == 'default' or not ipaddress.IPv4Network(r['dst'], strict=False).overlaps(ipaddress.IPv4Network(ADDRESS+'/32'))
    baseline = snapshot(); os.mkdir(ROOT, 0o750); os.chown(ROOT, 0, 1000)
    os.mkdir(ROOT/'server', 0o700); os.chown(ROOT/'server', 1000, 1000)
    ns = os.stat('/proc/1/ns/net'); exe = os.stat('/usr/bin/python3')
    value = {'pending': 'start', 'directory': [ROOT.stat().st_dev, ROOT.stat().st_ino], 'network': [ns.st_dev, ns.st_ino],
        'exe': [exe.st_dev, exe.st_ino], 'baseline': baseline, 'alias': 'karyo-qa-exit:'+secrets.token_hex(16)}
    fd = os.open(ROOT/'receipt.json', os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW, 0o600)
    try: os.write(fd, json.dumps(value).encode())
    finally: os.close(fd)
    run('/usr/sbin/ip', 'link', 'add', 'name', INTERFACE, 'type', 'dummy')
    value['index'] = link()['ifindex']
    run('/usr/sbin/ip', 'link', 'set', 'dev', INTERFACE, 'alias', value['alias'])
    run('/usr/sbin/ip', 'address', 'add', ADDRESS+'/32', 'dev', INTERFACE)
    run('/usr/sbin/ip', 'link', 'set', 'dev', INTERFACE, 'up')
    properties = ['Type=exec', 'Restart=no', 'KillMode=control-group', 'User=qaadmin', 'Group=qaadmin',
        'NoNewPrivileges=yes', 'CapabilityBoundingSet=', 'PrivateTmp=yes', 'PrivateDevices=yes', 'ProtectSystem=strict',
        'ProtectHome=yes', 'ReadWritePaths='+str(ROOT/'server'), 'InaccessiblePaths=-/var/lib/karyo/profile-vpn']
    subprocess.run(['/usr/bin/systemd-run', '--quiet', '--collect', '--unit='+UNIT,
        *('--property='+p for p in properties), '--', '/usr/bin/python3', '-I', __file__, '--server'], check=True)
    value['service'] = unit(); assert value['service']['ActiveState'] == 'active'
    value['start'] = NativeProcess.read(int(value['service']['MainPID']), tuple(value['exe']), 1000,
        value['service']['ControlGroup'], tuple(value['network']))
    value['pending'] = None; (ROOT/'receipt.json').write_text(json.dumps(value)+'\n')
    deadline = time.monotonic()+15
    while not (ROOT/'server/ready.json').exists():
        live(value); assert time.monotonic() < deadline; time.sleep(.1)
    ready = json.loads((ROOT/'server/ready.json').read_text())
    assert ready == {'pid': int(value['service']['MainPID']), 'address': ADDRESS, 'port': PORT}
    live(capture()); print(json.dumps({'ready': True, 'address': ADDRESS, 'port': PORT, 'uid': 1000, 'outside_profile_namespace': True}))


def cleanup():
    idle(); value = capture(); live(value)
    run('/usr/bin/systemctl', '--job-mode=fail', 'stop', UNIT)
    assert process_gone(int(value['service']['MainPID']), value['start']) and unit()['LoadState'] == 'not-found'
    capture(); run('/usr/sbin/ip', 'link', 'delete', 'dev', INTERFACE)
    assert snapshot() == value['baseline']
    assert set(p.name for p in ROOT.iterdir()) == {'server', 'receipt.json'} and shutil.rmtree.avoids_symlink_attacks
    assert [ROOT.stat().st_dev, ROOT.stat().st_ino] == value['directory']; shutil.rmtree(ROOT)
    print('PASS captured external endpoint, dummy link and owned fixture removed; original host network restored')


if sys.argv[1:] == ['prepare']: prepare()
elif sys.argv[1:] == ['status']: live(capture()); print(json.dumps({'ready': True, 'address': ADDRESS, 'port': PORT}))
elif sys.argv[1:] == ['cleanup']: cleanup()
else: raise SystemExit('Use prepare, status or cleanup on the fixed disposable host')
