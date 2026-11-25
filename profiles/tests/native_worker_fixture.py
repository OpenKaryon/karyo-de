# SPDX-License-Identifier: GPL-3.0-or-later
"""Neutral managed entry fixture; fixed FD3 bootstrap and FD4 lifecycle."""
import fcntl
import hashlib
import select
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
from karyo_profiles.worker_control import receive, send


def node(path):
    value = os.stat(path)
    return [str(value.st_dev), str(value.st_ino)]


def main():
    assert os.getuid() == 0
    seals = fcntl.fcntl(3, fcntl.F_GET_SEALS)
    assert seals & 15 == 15
    os.lseek(3, 0, os.SEEK_SET)
    data = os.read(3, 65537)
    assert len(data) <= 65536
    bootstrap = json.loads(data)
    assert bootstrap['role'] == 'managed-worker-bootstrap'
    channel = socket.socket(fileno=4)
    peer = struct.unpack('3i', channel.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    assert peer[0] == bootstrap['owner']['pid'] and peer[1:] == (0, 0)
    assert node('/proc/self/ns/net') == bootstrap['context']['network']
    assert node('/run/dbus/system_bus_socket') == bootstrap['context']['bus']['socket']
    assert node('/run/systemd/resolve') == bootstrap['context']['resolver']['directory']
    assert not Path('/run/systemd/private').exists()
    baseline = Path('/etc/resolv.conf').read_text()
    def packet(kind,value):
        return {'version':1,kind:value,'instance':bootstrap['worker']['id'],'generation':'1'}
    recovery = bootstrap['worker']['mode'] == 'recover'
    listener = None
    if not recovery:
        listener = socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET|socket.SOCK_CLOEXEC)
        listener.bind(bootstrap['directories']['endpoint'])
        os.chmod(bootstrap['directories']['endpoint'],0o660)
        listener.listen(1)
    if '--ready-delay' in sys.argv:time.sleep(1.5)
    send(channel,packet('event','ready'),None if listener is None else listener.fileno())
    initial = receive(channel)[0]
    if initial == packet('operation','retire'):
        send(channel,dict(packet('event','retired'),state='clean'))
        assert receive(channel)[0] == packet('operation','finish')
        if listener is not None:listener.close()
        channel.close()
        return
    assert initial == packet('operation','commit')

    state = Path(bootstrap['directories']['state']['path'])/'fixture-routes.json'
    if recovery:
        commands = json.loads(state.read_text()) if state.exists() else []
        for command in reversed(commands):
            subprocess.run(command,check=True,capture_output=True,timeout=5)
        state.unlink(missing_ok=True)
        send(channel,packet('event','committed'))
        assert receive(channel)[0] == packet('operation','retire')
        send(channel,dict(packet('event','retired'),state='clean'))
        assert receive(channel)[0] == packet('operation','finish')
        channel.close()
        return
    # This adds only a namespace-local temporary link. It configures no route,
    # DNS upstream, physical interface, host service or application credentials.
    tun = os.open('/dev/net/tun', os.O_RDWR | os.O_CLOEXEC)
    undo = []
    def create(command, cleanup):
        subprocess.run(command, check=True, capture_output=True, timeout=5)
        undo.append(cleanup)
        state.write_text(json.dumps(undo))
    try:
        name = b'karyo-fixture0'
        fcntl.ioctl(tun, 0x400454ca, struct.pack('16sH22x', name, 0x0001 | 0x1000 | 0x8000))
        for family, prefix in (('-4', '203.0.113.0/24'), ('-6', '2001:db8:abcd::/64')):
            route = ['unreachable', prefix, 'proto', '200', 'metric', '42000']
            create(['/usr/sbin/ip', family, 'route', 'add', *route],
                   ['/usr/sbin/ip', family, 'route', 'del', *route])
        route = ['unreachable', '198.51.100.0/24', 'table', '42000']
        create(['/usr/sbin/ip', '-4', 'route', 'add', *route],
               ['/usr/sbin/ip', '-4', 'route', 'del', *route])
        rule = ['fwmark', '0x123400', 'table', '42000', 'priority', '20000']
        create(['/usr/sbin/ip', '-4', 'rule', 'add', *rule],
               ['/usr/sbin/ip', '-4', 'rule', 'del', *rule])
        create(['/usr/sbin/nft', 'add', 'table', 'inet', 'karyo_fixture'],
               ['/usr/sbin/nft', 'delete', 'table', 'inet', 'karyo_fixture'])
        dns = subprocess.run(['/usr/bin/resolvectl', 'status'], capture_output=True,
                             text=True, timeout=5)
        assert dns.returncode == 0, dns.stderr
        send(channel,packet('event','committed'))
        try:
            while True:
                readable,unused,unused = select.select([channel,listener],[],[])
                if channel in readable:
                    retire = receive(channel)[0]
                    break
                if listener in readable:
                    connection,unused = listener.accept()
                    with connection:
                        connection.settimeout(3)
                        peer = struct.unpack('3i',connection.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
                        gui = bootstrap['gui']
                        assert peer == (gui['pid'],gui['uid'],gui['gid'])
                        start = Path('/proc',str(peer[0]),'stat').read_text().rpartition(') ')[2].split()[19]
                        assert start == gui['start']
                        assert receive(connection)[0] == {'operation':'bootstrap-digest'}
                        send(connection,{'prepared_sha256':hashlib.sha256(data).hexdigest()})
        except ValueError:
            if '--leave-routes-on-owner-loss' in sys.argv:
                os._exit(17)  # Own normal crash/recovery fixture, not a host fault.
            raise
        assert retire == packet('operation','retire')
        os.close(tun)
        tun = None
        if '--control-exercise' in sys.argv:
            send(channel,packet('event','link_closed'))
            assert receive(channel)[0] == packet('operation','finish_cleanup')
    finally:
        if tun is not None:
            os.close(tun)
        for command in reversed(undo):
            subprocess.run(command, check=True, capture_output=True, timeout=5)
    state.unlink(missing_ok=True)
    assert Path('/etc/resolv.conf').read_text() == baseline
    send(channel,dict(packet('event','retired'),state='clean'))
    assert receive(channel)[0] == packet('operation','finish')
    if listener is not None:
        listener.close()
    channel.close()


if __name__ == '__main__':
    main()
