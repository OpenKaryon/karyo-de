#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary main-network/DNS acceptance on the two independent networking VMs.

Creates temporary offline accounts and captured networks, not graphical logins
or VPNs. Local DNS and TCP/UDP endpoints avoid external credentials. No installed
module, host resolver, forwarding sysctl or host default route is replaced.
"""
import hashlib
import json
import os
from pathlib import Path
import pwd
import grp
import secrets
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import discover_dns, supported_resolver_layout
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_network import JournalNetwork, offline
from karyo_profiles.logind import Sessions
from karyo_profiles.networks import NetworkCommand
from karyo_profiles.storage import ConfigStore
from karyo_profiles.underlay import discover_underlay


def require(value, message):
    if not value:
        raise RuntimeError(message)


def run(*args, input=None):
    return subprocess.run(args, input=input, text=True, check=True,
                          capture_output=True, timeout=15).stdout


def host_snapshot(command):
    resolver = Path('/etc/resolv.conf')
    return {
        'routes': json.loads(command.run('/usr/sbin/ip', '-4', '-j', 'route', 'show', 'table', 'all')),
        'rules': json.loads(command.run('/usr/sbin/ip', '-4', '-j', 'rule', 'show')),
        'links': [(row['ifindex'], row['ifname'], row.get('ifalias')) for row in
                  json.loads(command.run('/usr/sbin/ip', '-j', 'link', 'show'))],
        'namespaces': command.run('/usr/sbin/ip', 'netns', 'list'),
        'forwarding': Path('/proc/sys/net/ipv4/ip_forward').read_text(),
        'resolver_target': str(resolver.resolve()),
        'resolver_hash': hashlib.sha256(resolver.read_bytes()).hexdigest(),
    }


class Endpoints:
    """One ordinary local DNS/TCP/UDP fixture on an owned host-veth address."""
    def __init__(self, host, label, answer):
        self.label, self.answer = label, answer
        self.domain = label + '.context.test'
        self.dns = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sockets = (self.dns, self.udp, self.tcp)
        try:
            self.dns.bind((host, 53))
            self.udp.bind((host, 0))
            self.tcp.bind((host, 0))
            self.tcp.listen(4)
        except BaseException:
            self.close()
            raise
        self.stopping = threading.Event()
        self.error = None
        self.requests = 0
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        try:
            while not self.stopping.is_set():
                ready, _, _ = select.select(self.sockets, [], [], .1)
                for server in ready:
                    if server is self.tcp:
                        connection, _ = server.accept()
                        with connection:
                            connection.sendall(self.label.encode())
                    elif server is self.udp:
                        _, peer = server.recvfrom(512)
                        server.sendto(self.label.encode(), peer)
                    else:
                        data, peer = server.recvfrom(512)
                        offset, labels = 12, []
                        while data[offset]:
                            length = data[offset]
                            labels.append(data[offset+1:offset+1+length].decode('ascii'))
                            offset += length + 1
                        offset += 1
                        kind, klass = struct.unpack('!HH', data[offset:offset+4])
                        question = data[12:offset+4]
                        matched = '.'.join(labels) == self.domain and klass == 1
                        answered = matched and kind == 1
                        header = data[:2] + struct.pack('!HHHHH', 0x8180 if matched else 0x8183,
                                                        1, int(answered), 0, 0)
                        answer = (b'\xc0\x0c' + struct.pack('!HHIH', 1, 1, 0, 4)
                                  + socket.inet_aton(self.answer)) if answered else b''
                        server.sendto(header + question + answer, peer)
                        self.requests += 1
        except BaseException as error:
            self.error = type(error).__name__

    def close(self):
        if hasattr(self, 'stopping'):
            self.stopping.set()
            self.thread.join(timeout=2)
            require(not self.thread.is_alive(), 'Fixture endpoint did not stop')
        for server in self.sockets:
            server.close()


def client(dns, endpoint):
    account = dns.files.accounts.by_uid(dns.network.network.spec.uid)
    network = dns.network.network
    code = '''import json,socket,sys
domain,answer,host,tcp,udp,label=sys.argv[1:]
values={row[4][0] for row in socket.getaddrinfo(domain,None,socket.AF_INET)}
assert values=={answer},values
with socket.create_connection((host,int(tcp)),timeout=3) as stream:
 assert stream.recv(64).decode()==label
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as stream:
 stream.settimeout(3);stream.sendto(b'ordinary payload',(host,int(udp)))
 assert stream.recv(64).decode()==label
neighbor=('work' if label=='home' else 'home')+'.context.test'
try: socket.getaddrinfo(neighbor,None,socket.AF_INET)
except socket.gaierror: pass
else: raise RuntimeError('Another context DNS view was returned')
print(json.dumps({'dns':True,'tcp':True,'udp':True,'separate_dns_view':True}))
'''
    result = run('/usr/bin/systemd-run', '--quiet', '--wait', '--pipe', '--collect',
                 '--unit=karyo-main-probe-' + network.spec.token,
                 '--property=User=' + str(account.uid), '--property=Group=' + str(account.gid),
                 *('--property=' + value for value in dns.files.plan.client_properties()),
                 '--', '/usr/bin/python3', '-I', '-c', code, endpoint.domain, endpoint.answer,
                 endpoint.tcp.getsockname()[0], str(endpoint.tcp.getsockname()[1]),
                 str(endpoint.udp.getsockname()[1]), endpoint.label)
    return json.loads(result)


def main():
    require(os.getuid() == os.geteuid() == 0 and len(sys.argv) == 1, 'Use root without arguments')
    require(socket.gethostname() in ('karyo-network-debian', 'karyo-network-ubuntu'),
            'Use only the independent networking QA copies')
    parent = Path('/var/lib/karyo-qa')
    parent.mkdir(mode=0o755, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='main-network-', dir=parent))
    print('Fixture state: ' + str(root), flush=True)
    command = NetworkCommand()
    before = host_snapshot(command)
    supported_resolver_layout()
    seed = discover_dns(discover_underlay(command), command)
    accounts, sessions = LocalAccounts(), Sessions()
    store = ConfigStore(root/'config', root/'no-legacy', root/'runtime')
    created, networks, resolvers, endpoints = [], [], [], []
    evidence = {'kind': 'source-native-main-contexts', 'graphical_or_vpn': False}
    with RuntimeJournal(root/'journal') as journal:
        def start_context(account, label):
            network = JournalNetwork.prepare_main(journal, store, account,
                                                  accounts=accounts, sessions=sessions, command=command)
            networks.append(network)
            dns = JournalDNS.start(journal, network, seed, accounts=accounts)
            resolvers.append(dns)
            endpoint = Endpoints(network.network.spec.host, label,
                                 {'home': '198.18.0.1', 'work': '198.18.0.2',
                                  'experiments': '198.18.0.3'}[label])
            endpoints.append(endpoint)
            # The fixture DNS endpoint is directly on its owned veth; no
            # global forwarding or public service is needed for this slice.
            command.run('/usr/bin/busctl', '--address=' + dns.files.plan.bus_address,
                        '--timeout=3s', 'call', 'org.freedesktop.resolve1',
                        '/org/freedesktop/resolve1', 'org.freedesktop.resolve1.Manager',
                        'SetLinkDNS', 'ia(iay)', str(network.network.indices[1]),
                        '1', '2', '4', *network.network.spec.host.split('.'))
            return client(dns, endpoint)

        try:
            for label in ('home', 'work', 'experiments'):
                uids = {item.pw_uid for item in pwd.getpwall()}
                gids = {item.gr_gid for item in grp.getgrall()}
                uid = next(value for value in range(16000, 16100) if value not in uids | gids)
                name = 'karyo-net-' + str(uid)
                home = Path('/home') / name
                require(not home.exists(), 'Fixture home already exists')
                run('/usr/sbin/useradd', '-m', '-U', '-u', str(uid), '-s', '/bin/bash', name)
                account = accounts.by_uid(uid)
                created.append((account, (home.stat().st_dev, home.stat().st_ino)))
                run('/usr/sbin/chpasswd', input=name + ':' + secrets.token_urlsafe(32) + '\n')
                with store.locked():
                    evidence[label] = start_context(account, label)
            require(len({lease.network.identity for lease in networks}) == 3, 'Namespace identities overlap')
            require(len({value.services.resolver.identity.pid for value in resolvers}) == 3,
                    'Resolver instances overlap')
            for dns, endpoint in zip(resolvers, endpoints):
                require(endpoint.error is None and endpoint.requests > 0, 'DNS fixture failed')
                evidence[endpoint.label + '_simultaneous'] = client(dns, endpoint)
            evidence['distinct_namespaces_and_resolvers'] = True
            neighbors = [(network.network.identity, dns.services.resolver.identity)
                         for network, dns in zip(networks[1:], resolvers[1:])]
            previous_home = networks[0].token
            with store.locked():
                endpoints[0].close()
                resolvers[0].close()
                networks[0].close()
                endpoints.pop(0); resolvers.pop(0); networks.pop(0)
                for dns, endpoint in zip(resolvers, endpoints):
                    evidence[endpoint.label + '_while_home_stopped'] = client(dns, endpoint)
                require(neighbors == [(network.network.identity, dns.services.resolver.identity)
                                      for network, dns in zip(networks, resolvers)],
                        'A neighboring network or resolver was replaced')
                evidence['home_restarted'] = start_context(created[0][0], 'home')
                require(networks[-1].token != previous_home, 'Home reused its old lifetime')
                for dns, endpoint in zip(resolvers[:-1], endpoints[:-1]):
                    evidence[endpoint.label + '_after_home_restart'] = client(dns, endpoint)
                require(neighbors == [(network.network.identity, dns.services.resolver.identity)
                                      for network, dns in zip(networks[:-1], resolvers[:-1])],
                        'Home restart changed a neighboring lifetime')
            evidence['fresh_main_lifetime_and_unchanged_neighbors'] = True
        finally:
            for endpoint in reversed(endpoints):
                endpoint.close()
            with store.locked():
                for dns in reversed(resolvers):
                    dns.close()
                for network in reversed(networks):
                    network.close()
            require(not journal.snapshot()['profiles'], 'Retain accounts while runtime receipts remain')
            for account, identity in reversed(created):
                offline(account, accounts, sessions)
                home = Path(account.home)
                require((home.stat().st_dev, home.stat().st_ino) == identity,
                        'Fixture home changed; retain account')
                run('/usr/sbin/userdel', '-r', account.name)
    require(host_snapshot(command) == before, 'Host network or resolver did not return to baseline')
    evidence['captured_cleanup_and_host_baseline'] = True
    (root/'result.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
