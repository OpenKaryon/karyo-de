#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Disposable real TLS peers for the distribution's OpenVPN NM plugin."""
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import time

from native_main_network import Endpoints, client, require, run


class OpenVPNPeer:
    def __init__(self, scope, label):
        self.scope, self.label = scope, label
        number = {'home': '1', 'work': '2', 'experiments': '3'}[label]
        self.host, self.answer = '198.19.' + number + '.1', '198.18.0.' + number
        self.name = 'kqo' + secrets.token_hex(5)
        self.directory = Path('/etc/openvpn') / ('karyo_qa_' + self.name)
        self.connection, self.interface = label + '-fixture', 'tun0'
        self.endpoint = self.process = self.log = None
        self.import_file = Path(scope.state.account.home) / 'openvpn_fixture.ovpn'

    def start(self, ordinary_cli):
        self.directory.mkdir(mode=0o700)
        root = self.directory
        run('/usr/bin/openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-noenc', '-sha256', '-days', '1',
            '-subj', '/CN=Karyo QA CA', '-keyout', str(root/'ca.key'), '-out', str(root/'ca.crt'))
        for name, usage in (('server', 'serverAuth'), ('client', 'clientAuth')):
            run('/usr/bin/openssl', 'req', '-newkey', 'rsa:2048', '-noenc', '-sha256', '-subj', '/CN=' + name,
                '-keyout', str(root/(name + '.key')), '-out', str(root/(name + '.csr')))
            ext = root/(name + '.ext')
            ext.write_text('basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=' + usage + '\n')
            run('/usr/bin/openssl', 'x509', '-req', '-sha256', '-days', '1', '-in', str(root/(name + '.csr')),
                '-CA', str(root/'ca.crt'), '-CAkey', str(root/'ca.key'), '-CAcreateserial', '-extfile', str(ext),
                '-out', str(root/(name + '.crt')))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind((self.scope.network.spec.host, 0))
            port = reservation.getsockname()[1]
        config = root/'server.conf'
        config.write_text('local ' + self.scope.network.spec.host + '\nport ' + str(port) + '\nproto udp4\n'
            'dev ' + self.name + '\ndev-type tun\ntopology subnet\nserver ' + self.host[:-1] + '0 255.255.255.0\n'
            'ca ' + str(root/'ca.crt') + '\ncert ' + str(root/'server.crt') + '\nkey ' + str(root/'server.key') + '\n'
            'dh none\ndata-ciphers AES-256-GCM\nkeepalive 5 20\nverb 3\n'
            'push "dhcp-option DNS ' + self.host + '"\npush "redirect-gateway def1"\n')
        self.log = (root/'server.log').open('xb')
        self.process = subprocess.Popen(['/usr/sbin/openvpn', '--config', str(config)],
                                        stdin=subprocess.DEVNULL, stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 8
        while not Path('/sys/class/net', self.name).exists():
            require(self.process.poll() is None and time.monotonic() < deadline, 'The disposable TLS server did not start')
            time.sleep(.05)
        self.endpoint = Endpoints(self.host, self.label, self.answer)
        text = ('client\ndev tun\nproto udp4\nremote ' + self.scope.network.spec.host + ' ' + str(port) + '\n'
                'remote-cert-tls server\ndata-ciphers AES-256-GCM\n'
                'ca ' + str(root/'ca.crt') + '\ncert ' + str(root/'client.crt') + '\nkey ' + str(root/'client.key') + '\n')
        with self.import_file.open('x') as stream:
            os.chmod(self.import_file, 0o600)
            stream.write(text)
        account = self.scope.state.account
        os.chown(self.import_file, account.uid, account.gid)
        ordinary_cli(self.scope, 'connection', 'import', 'type', 'openvpn', 'file', str(self.import_file))
        ordinary_cli(self.scope, 'connection', 'modify', 'openvpn_fixture', 'connection.id', self.connection,
                     'connection.autoconnect', 'no', 'ipv4.dns-priority', '-50', 'ipv4.dns-search', '~.')
        ordinary_cli(self.scope, 'connection', 'up', self.connection, 'ifname', 'eth0')
        self.probe()

    def probe(self):
        value = client(self.scope.dns, self.endpoint)
        route = json.loads(run('/usr/sbin/ip', 'netns', 'exec', self.scope.network.spec.name,
                               '/usr/sbin/ip', '-4', '-j', 'route', 'get', '1.1.1.1'))
        require(len(route) == 1 and route[0].get('dev') == self.interface,
                'The private VPN route was not selected: ' + json.dumps(route))
        require(self.endpoint.requests > 0, 'No DNS query reached the TLS endpoint')
        self.log.flush()
        require('Peer Connection Initiated' in (self.directory/'server.log').read_text(), 'No real TLS client was observed')
        value.update(openvpn_tls_peer=True, encrypted_peer_payload=True, private_default_route=True)
        return value

    def close(self):
        if self.endpoint is not None:self.endpoint.close();self.endpoint = None
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
                try:self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:self.process.kill();self.process.wait(timeout=3)
            self.process = None
        if self.log is not None:self.log.close();self.log = None
        self.import_file.unlink(missing_ok=True)
        # Keep this root-only fixture evidence on the QA VM. Its generated
        # credentials are not deployment inputs and are never exported.

    def wait_disconnected(self):
        deadline = time.monotonic() + 5
        while True:
            links = json.loads(run('/usr/sbin/ip', 'netns', 'exec', self.scope.network.spec.name,
                                   '/usr/sbin/ip', '-j', '-d', 'link', 'show'))
            if not any(row.get('linkinfo', {}).get('info_kind') == 'tun' for row in links):return
            require(time.monotonic() < deadline, 'The disconnected OpenVPN child retained its TUN device')
            time.sleep(.05)


if __name__ == '__main__':
    from native_network_manager import main
    main(tunnels='openvpn')
