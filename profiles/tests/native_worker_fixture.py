# SPDX-License-Identifier: GPL-3.0-or-later
"""Neutral managed entry fixture; fixed FD3 bootstrap and FD4 lifecycle."""
import fcntl
import json
import os
from pathlib import Path
import socket
import struct
import subprocess


def node(path):
    value = os.stat(path)
    return [value.st_dev, value.st_ino]


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
    # This adds only a namespace-local temporary link. It configures no route,
    # DNS upstream, physical interface, host service or application credentials.
    tun = os.open('/dev/net/tun', os.O_RDWR | os.O_CLOEXEC)
    try:
        name = b'karyo-fixture0'
        fcntl.ioctl(tun, 0x400454ca, struct.pack('16sH22x', name, 0x0001 | 0x1000 | 0x8000))
        dns = subprocess.run(['/usr/bin/resolvectl', 'status'], capture_output=True,
                             text=True, timeout=5)
        assert dns.returncode == 0, dns.stderr
        ready = {'version': 1, 'event': 'ready', 'pid': os.getpid(),
                 'network': node('/proc/self/ns/net'), 'mount': node('/proc/self/ns/mnt'),
                 'resolver': node('/run/systemd/resolve'), 'bus': node('/run/dbus/system_bus_socket'),
                 'bootstrap_sealed': True, 'owner_peer': True,
                 'tun_created': name.decode(), 'private_resolvectl': True,
                 'host_service_socket_absent': True, 'resolver_file': baseline}
        channel.send(json.dumps(ready).encode())
        retire = json.loads(channel.recv(4096))
        assert retire == {'version': 1, 'operation': 'retire',
                          'instance': bootstrap['worker']['id'], 'generation': 1}
    finally:
        os.close(tun)
    assert Path('/etc/resolv.conf').read_text() == baseline
    channel.send(json.dumps({'version': 1, 'event': 'retired', 'state': 'clean'}).encode())
    channel.close()


if __name__ == '__main__':
    main()
