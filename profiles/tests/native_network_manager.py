#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit native private-NM feasibility and ordinary two-context lifecycle.

Uses the unchanged distribution daemon and nmcli. This is a real service/state
check, not graphical acceptance or a successful VPN handshake claim.
"""
import json
import os
from pathlib import Path
import grp
import pwd
import secrets
import socket
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import discover_dns
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_network import JournalNetwork, offline
from karyo_profiles.logind import Sessions
from karyo_profiles.network_manager import NetworkManagerScope
from karyo_profiles.networks import NetworkCommand
from karyo_profiles.storage import ConfigStore
from karyo_profiles.underlay import discover_underlay
from native_main_network import Endpoints, client, host_snapshot, require, run


def host_configuration():
    import hashlib
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
            for root in (Path('/etc/NetworkManager'), Path('/etc/netplan'))
            for path in root.rglob('*') if path.is_file() and not path.is_symlink()}


def ordinary_cli(scope, *arguments):
    plan = scope.files.plan
    name = 'karyo-nm-client-' + secrets.token_hex(8)
    options = ['/usr/bin/systemd-run', '--quiet', '--wait', '--pipe', '--collect', '--unit=' + name,
               '--property=User=' + str(scope.network.spec.uid), '--property=Group=' + str(plan.gid),
               '--property=Environment=DBUS_SYSTEM_BUS_ADDRESS=' + plan.bus_address]
    options += ['--property=' + value for value in plan.client_properties()]
    result = subprocess.run([*options, '/usr/bin/nmcli', '--wait', '5', *arguments], capture_output=True,
                            text=True, timeout=15)
    require(result.returncode == 0, 'Fixed ordinary nmcli operation failed: ' + result.stderr.strip())
    return result.stdout


def fixture_connections(scope):
    return [name for name in ordinary_cli(scope, '--terse', '--fields', 'NAME', 'connection', 'show').splitlines()
            if name.endswith('-fixture')]


def main(*, tunnels=False):
    require(os.getuid() == os.geteuid() == 0 and len(sys.argv) == 1, 'Root QA entry required')
    require(socket.gethostname() in ('karyo-network-debian', 'karyo-network-ubuntu'), 'Use independent networking QA copies')
    root = Path(tempfile.mkdtemp(prefix='network-manager-', dir='/var/lib/karyo-qa'))
    print('NetworkManager fixture: ' + str(root), flush=True)
    command, accounts, sessions = NetworkCommand(), LocalAccounts(), Sessions()
    before, host_nm = host_snapshot(command), host_configuration()
    seed = discover_dns(discover_underlay(command), command)
    store = ConfigStore(root/'config', root/'no-legacy', root/'runtime')
    contexts, created = [], []
    evidence = {'unchanged_distribution_binary': command.run('/usr/sbin/NetworkManager', '--version').strip()}
    with RuntimeJournal(root/'journal') as journal:
        try:
            for label in (('home', 'work', 'experiments') if tunnels else ('home', 'work')):
                occupied = {row.pw_uid for row in pwd.getpwall()} | {row.gr_gid for row in grp.getgrall()}
                uid = next(value for value in range(16000, 16100) if value not in occupied)
                name = 'karyo-nm-' + str(uid)
                require(not Path('/home', name).exists(), 'Existing QA home retained')
                run('/usr/sbin/useradd', '-m', '-U', '-u', str(uid), '-s', '/bin/bash', name)
                account = accounts.by_uid(uid)
                created.append(account)
                run('/usr/sbin/chpasswd', input=name + ':' + secrets.token_urlsafe(32) + '\n')
                with store.locked():
                    network = JournalNetwork.prepare_main(journal, store, account, accounts=accounts, sessions=sessions, command=command)
                    value = {'network': network, 'dns': None, 'scope': None, 'endpoint': None, 'label': label, 'tunnel': None}
                    contexts.append(value)
                    dns = JournalDNS.start(journal, network, seed, accounts=accounts, service_view='network-manager')
                    value['dns'] = dns
                    endpoint = Endpoints(network.network.spec.host, label, '198.18.0.' + str(len(contexts)))
                    value['endpoint'] = endpoint
                    command.run('/usr/bin/busctl', '--address=' + dns.files.plan.bus_address, '--timeout=3s',
                        'call', 'org.freedesktop.resolve1', '/org/freedesktop/resolve1', 'org.freedesktop.resolve1.Manager',
                        'SetLinkDNS', 'ia(iay)', str(network.network.indices[1]), '1', '2', '4', *network.network.spec.host.split('.'))
                    scope = NetworkManagerScope(dns, state_root=root/'state')
                    value['scope'] = scope
                    if tunnels:
                        before_record = journal.record(network.token)
                        journal.change(before_record, dict(before_record, network=dict(before_record['network'], contents='tunnel')))
                        network.network.contents = 'tunnel'
                    scope.start()
                    if tunnels == 'openvpn':
                        ordinary_cli(scope, 'connection', 'add', 'type', 'ethernet', 'ifname', 'eth0',
                                     'con-name', 'Karyo QA uplink', 'ipv4.method', 'manual',
                                     'ipv4.addresses', network.network.spec.guest + '/30',
                                     'ipv4.gateway', network.network.spec.host, 'ipv4.dns', network.network.spec.host,
                                     'ipv6.method', 'disabled')
                        ordinary_cli(scope, 'connection', 'up', 'Karyo QA uplink')
                    if tunnels:
                        if tunnels == 'openvpn':
                            from native_openvpn import OpenVPNPeer as Peer
                        else:
                            from native_wireguard import WireGuardPeer as Peer
                        value['tunnel'] = Peer(scope, label)
                        value['tunnel'].start(ordinary_cli)
                    else:
                        ordinary_cli(scope, 'connection', 'add', 'type', 'wireguard', 'ifname', 'wgqa',
                                     'con-name', label + '-fixture', 'connection.autoconnect', 'no',
                                     'ipv4.method', 'disabled', 'ipv6.method', 'disabled')
                    names = fixture_connections(scope)
                    require(names == [label + '-fixture'], 'The ordinary client reached another connection store')
                    evidence[label] = value['tunnel'].probe() if tunnels else client(dns, endpoint)
                    evidence[label]['private_nm_pid'] = scope.unit.identity.pid
                    evidence[label]['state_directory'] = list(scope.state.identity)
                    persistent = (scope.state.path/'netplan/etc' if scope.netplan
                                  else scope.state.path/'etc/system-connections')
                    pattern = '*.yaml' if scope.netplan else '*.nmconnection'
                    require(any(persistent.glob(pattern)), 'No private connection configuration was saved')
                    evidence[label]['configuration_backend'] = 'netplan' if scope.netplan else 'keyfile'
                    if tunnels:
                        # Experiments starts after both other VPNs are active.
                        # Its creation must preserve every existing payload path.
                        for existing in contexts:existing['tunnel'].probe()
            require(contexts[0]['scope'].unit.identity.pid != contexts[1]['scope'].unit.identity.pid, 'Manager instances overlap')
            with store.locked():
                home, work = contexts[:2]
                work_identity = work['scope'].unit.identity
                home_state = home['scope'].state.identity
                home_pid = home['scope'].unit.identity.pid
                if tunnels:
                    ordinary_cli(home['scope'], 'connection', 'down', 'home-fixture')
                    if tunnels == 'openvpn':home['tunnel'].wait_disconnected()
                    client(home['dns'], home['endpoint']);work['tunnel'].probe()
                    ordinary_cli(home['scope'], 'connection', 'up', 'home-fixture',
                                 *(('ifname', 'eth0') if tunnels == 'openvpn' else ()))
                    for existing in contexts:existing['tunnel'].probe()
                    ordinary_cli(home['scope'], 'connection', 'down', 'home-fixture')
                    if tunnels == 'openvpn':home['tunnel'].wait_disconnected()
                home['scope'].close()
                require(fixture_connections(work['scope']) == ['work-fixture'], 'Stopping Home changed Work')
                work['tunnel'].probe() if tunnels else client(work['dns'], work['endpoint'])
                replacement = NetworkManagerScope(home['dns'], state_root=root/'state')
                home['scope'] = replacement
                replacement.start()
                require(replacement.unit.identity.pid != home_pid and replacement.state.identity == home_state,
                        'Worker restart did not retain its own persistent state')
                require(fixture_connections(replacement) == ['home-fixture'], 'Home did not reload its private connection')
                require(work['scope'].unit.identity == work_identity, 'Home restart replaced Work')
                if tunnels:
                    home['tunnel'].scope = replacement
                    if tunnels == 'openvpn':ordinary_cli(replacement, 'connection', 'up', 'Karyo QA uplink')
                    ordinary_cli(replacement, 'connection', 'up', 'home-fixture',
                                 *(('ifname', 'eth0') if tunnels == 'openvpn' else ()))
                    for existing in contexts:existing['tunnel'].probe()
                else:
                    client(home['dns'], home['endpoint']);client(work['dns'], work['endpoint'])
            evidence['two_private_instances_and_ordinary_clients'] = True
            evidence['restart_preserves_own_state_and_neighbor'] = True
            evidence['real_vpn_handshake'] = bool(tunnels)
            if tunnels:evidence['protocol'] = 'openvpn' if tunnels == 'openvpn' else 'wireguard'
            if tunnels:
                evidence['disconnect_reconnect_and_worker_restart_neighbor_payloads'] = True
                evidence['later_experiments_preserves_home_work_payloads'] = True
        finally:
            with store.locked():
                for value in reversed(contexts):
                    scope = value['scope']
                    if scope is not None:scope.close()
                    if value['tunnel'] is not None:value['tunnel'].close()
                    if value['endpoint'] is not None:value['endpoint'].close()
                    if value['dns'] is not None:value['dns'].close()
                    value['network'].close()
            require(not journal.snapshot()['profiles'], 'Retain accounts while runtime resources remain')
            for account in reversed(created):
                offline(account, accounts, sessions)
                home = Path(account.home)
                require(home == Path('/home')/account.name and home.stat().st_uid == account.uid,
                        'The temporary account home changed')
                run('/usr/sbin/userdel', '-r', account.name)
    require(host_snapshot(command) == before and host_configuration() == host_nm, 'Host baseline changed')
    evidence['owned_cleanup_and_host_baseline'] = True
    (root/'result.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':main()
