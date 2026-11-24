#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real disposable WireGuard peers for the native two-context service gate.

Only temporary generated keys are used. No key material is printed or copied
into the repository. The peer endpoints and routes are owned QA objects.
"""
import json
import os
from pathlib import Path
import secrets
import time

from native_main_network import Endpoints, client, require, run


class WireGuardPeer:
    def __init__(self, scope, label):
        self.scope, self.label = scope, label
        self.name = 'kqw' + secrets.token_hex(5)
        self.index = None
        self.endpoint = None
        number = {'home': '1', 'work': '2', 'experiments': '3'}[label]
        self.host = '198.19.' + number + '.1'
        self.answer = '198.18.0.' + number
        self.address = self.host[:-1] + '2'
        # The stock Ubuntu wg AppArmor profile reads keys from this standard
        # directory. Use a unique exclusive file; do not change that policy.
        self.keyfile = Path('/etc/wireguard') / (self.name + '.key')
        self.key_identity = None
        self.connection = label + '-fixture'
        self.interface = 'wgqa'
        self.import_file = Path(scope.state.account.home) / (self.interface + '.conf')

    def start(self, ordinary_cli):
        private = run('/usr/bin/wg', 'genkey').strip()
        public = run('/usr/bin/wg', 'pubkey', input=private + '\n').strip()
        peer_private = run('/usr/bin/wg', 'genkey').strip()
        peer_public = run('/usr/bin/wg', 'pubkey', input=peer_private + '\n').strip()
        with self.keyfile.open('x') as stream:
            os.chmod(self.keyfile, 0o600)
            stream.write(private + '\n')
            info = os.fstat(stream.fileno())
            self.key_identity = info.st_dev, info.st_ino
        run('/usr/sbin/ip', 'link', 'add', self.name, 'type', 'wireguard')
        self.index = json.loads(run('/usr/sbin/ip', '-j', 'link', 'show', 'dev', self.name))[0]['ifindex']
        run('/usr/sbin/ip', 'link', 'set', 'dev', self.name, 'alias', 'karyo-qa-' + self.name)
        run('/usr/bin/wg', 'set', self.name, 'private-key', str(self.keyfile), 'listen-port', '0',
            'peer', peer_public, 'allowed-ips', self.address + '/32')
        run('/usr/sbin/ip', 'address', 'add', self.host + '/24', 'dev', self.name)
        run('/usr/sbin/ip', 'link', 'set', 'dev', self.name, 'up')
        port = run('/usr/bin/wg', 'show', self.name, 'listen-port').strip()
        self.endpoint = Endpoints(self.host, self.label, self.answer)
        content = ('[Interface]\nPrivateKey=' + peer_private + '\nAddress=' + self.address + '/24\n'
                   'DNS=' + self.host + '\n[Peer]\nPublicKey=' + public + '\n'
                   'Endpoint=' + self.scope.network.spec.host + ':' + port + '\n'
                   'AllowedIPs=0.0.0.0/0\nPersistentKeepalive=5\n')
        with self.import_file.open('x') as stream:
            os.chmod(self.import_file, 0o600)
            stream.write(content)
        account = self.scope.state.account
        os.chown(self.import_file, account.uid, account.gid)
        ordinary_cli(self.scope, 'connection', 'import', 'type', 'wireguard', 'file', str(self.import_file))
        ordinary_cli(self.scope, 'connection', 'modify', self.interface, 'connection.id', self.connection,
                     'connection.autoconnect', 'no',
                     'ipv4.dns-priority', '-50', 'ipv4.dns-search', '~.')
        ordinary_cli(self.scope, 'connection', 'up', self.connection)
        self.probe()

    def probe(self):
        deadline = time.monotonic() + 5
        while True:
            try:
                value = client(self.scope.dns, self.endpoint)
                handshakes = run('/usr/bin/wg', 'show', self.name, 'latest-handshakes').split()
                transfer = run('/usr/bin/wg', 'show', self.name, 'transfer').split()
                require(len(handshakes) == 2 and int(handshakes[1]) > 0, 'The WireGuard peer did not handshake')
                require(len(transfer) == 3 and int(transfer[1]) > 0 and int(transfer[2]) > 0,
                        'No encrypted peer payload was transferred')
                require(self.endpoint.requests > 0, 'No DNS query reached the encrypted endpoint')
                route = json.loads(run('/usr/sbin/ip', 'netns', 'exec', self.scope.network.spec.name,
                                       '/usr/sbin/ip', '-4', '-j', 'route', 'get', '1.1.1.1'))
                require(len(route) == 1 and route[0].get('dev') == self.interface,
                        'The private default route did not select its own VPN')
                value.update(wireguard_handshake=True, encrypted_peer_payload=True, private_default_route=True)
                return value
            except (RuntimeError, OSError):
                if time.monotonic() >= deadline:raise
                time.sleep(.1)

    def close(self):
        if self.endpoint is not None:self.endpoint.close();self.endpoint = None
        if self.index is not None:
            row = json.loads(run('/usr/sbin/ip', '-j', 'link', 'show', 'dev', self.name))[0]
            require(row['ifindex'] == self.index and row.get('ifalias') == 'karyo-qa-' + self.name,
                    'The temporary WireGuard peer identity changed')
            run('/usr/sbin/ip', 'link', 'delete', 'dev', self.name)
            self.index = None
        if self.key_identity is not None:
            info = self.keyfile.stat(follow_symlinks=False)
            require((info.st_dev, info.st_ino) == self.key_identity, 'The temporary WireGuard key identity changed')
            self.keyfile.unlink()
            self.key_identity = None
        self.import_file.unlink(missing_ok=True)


if __name__ == '__main__':
    from native_network_manager import main
    main(tunnels=True)
