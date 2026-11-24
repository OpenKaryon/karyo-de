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
import signal
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import DNSSeed, discover_dns
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_network import JournalNetwork, offline
from karyo_profiles.logind import Sessions
from karyo_profiles.journal_network_service import JournalNetworkService
from karyo_profiles.networks import NetworkCommand
from karyo_profiles.network_uplink import uplink_flags
from karyo_profiles.storage import ConfigStore
from karyo_profiles.services import ServiceError
from karyo_profiles.underlay import discover_underlay
from native_main_network import Endpoints, client, host_snapshot, require, run
from native_network_service_recovery import restore_context
from native_underlay_fixture import UnderlayAddress


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
    if result.returncode != 0 and arguments[:2] == ('connection', 'up'):
        print(scope.nmcli('-f', 'GENERAL.DEVICE,GENERAL.TYPE,GENERAL.STATE,GENERAL.REASON,GENERAL.NM-MANAGED',
                         'device', 'show'), flush=True)
    require(result.returncode == 0, 'Fixed ordinary nmcli operation failed: ' + result.stderr.strip())
    return result.stdout


def fixture_connections(scope):
    return [name for name in ordinary_cli(scope, '--terse', '--fields', 'NAME', 'connection', 'show').splitlines()
            if name.endswith('-fixture')]


def recover_in_fresh_process(journal, store, root, contexts, accounts):
    with store.locked():
        expected = {value['network'].token: value['scope'].unit.identity.invocation for value in contexts}
        for value in contexts:
            value['service'].release_handles();value['dns'].release_handles()
            os.close(value['network'].network.fd);value['network'].network.fd = None
    journal.__exit__(None, None, None)
    try:
        observed = json.loads(run('/usr/bin/python3', '-B', str(Path(__file__).with_name('native_network_service_recovery.py')), str(root)))
        require({row['token']: row['invocation'] for row in observed} == expected,
                'A fresh controller did not capture the same live worker generations')
    finally:
        # RuntimeJournal opens in its constructor; __enter__ is not a reopen.
        journal.__init__(root/'journal')
        with store.locked():
            for value in contexts:
                record = journal.record(value['network'].token)
                network, dns, service = restore_context(journal, store, root, record, accounts)
                value.update(network=network, dns=dns, service=service, scope=service.scope)
                if value['tunnel'] is not None:value['tunnel'].scope = service.scope
    return observed


def retire_through_runtime(journal, store, root, contexts):
    with store.locked():
        tokens = {value['network'].token for value in contexts}
        for value in contexts:
            value['service'].release_handles();value['dns'].release_handles()
            os.close(value['network'].network.fd);value['network'].network.fd = None
    journal.close()
    try:
        result = subprocess.run(['/usr/bin/python3', '-B', str(Path(__file__).with_name('native_network_service_recovery.py')),
                                 str(root), '--retire'], capture_output=True, text=True, timeout=45, check=True)
        require(set(json.loads(result.stdout)['retired']) == tokens, 'Generic recovery retired another context set')
    finally:journal.__init__(root/'journal')
    require(not journal.snapshot()['profiles'], 'Generic recovery left network-service receipts')
    for value in contexts:value['retired_by_runtime'] = True


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
                    value = {'network': network, 'dns': None, 'scope': None, 'service': None,
                             'endpoint': None, 'label': label, 'tunnel': None, 'physical': None}
                    contexts.append(value)
                    address = UnderlayAddress('192.0.2.' + str(9 + len(contexts)), network.token)
                    value['physical'] = address
                    address.create()
                    endpoint = Endpoints(address.address, label, '198.18.0.' + str(len(contexts)))
                    value['endpoint'] = endpoint
                    physical_seed = DNSSeed(seed.physical, (address.address,), seed.search)
                    dns = JournalDNS.start(journal, network, physical_seed, accounts=accounts, service_view='network-manager')
                    value['dns'] = dns
                    if not tunnels and label == 'home':
                        from unittest.mock import patch
                        with patch('karyo_profiles.network_uplink.prepare_uplink', side_effect=ServiceError('QA uplink setup failure')):
                            try:JournalNetworkService.start(journal, dns, state_root=root/'state')
                            except ServiceError:pass
                            else:raise RuntimeError('The controlled uplink preparation failure was ignored')
                        proof = journal.record(network.token)['network_service']
                        require(proof['pending'] == 'uplink-prepare' and proof['unit'] is not None,
                                'Uplink preparation lost its acknowledged worker receipt')
                        incomplete = JournalNetworkService.restore(journal, dns, state_root=root/'state')
                        incomplete.close()
                        require('network_service' not in journal.record(network.token), 'The failed setup worker was not retired')
                        evidence['failed_uplink_setup_retired_known_worker'] = True
                    service = JournalNetworkService.start(journal, dns, state_root=root/'state')
                    value['service'] = service
                    scope = service.scope
                    value['scope'] = scope
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
                    evidence[label]['generated_uplink_flags'] = uplink_flags(scope)
                    if tunnels:
                        # Experiments starts after both other VPNs are active.
                        # Its creation must preserve every existing payload path.
                        for existing in contexts:existing['tunnel'].probe()
            require(contexts[0]['scope'].unit.identity.pid != contexts[1]['scope'].unit.identity.pid, 'Manager instances overlap')
            evidence['fresh_controller_observation'] = recover_in_fresh_process(journal, store, root, contexts, accounts)
            with store.locked():
                home, work = contexts[:2]
                work_identity = work['scope'].unit.identity
                home_state = home['scope'].state.identity
                home_pid = home['scope'].unit.identity.pid
                if tunnels:
                    ordinary_cli(home['scope'], 'connection', 'down', 'home-fixture')
                    if tunnels == 'openvpn':home['tunnel'].wait_disconnected()
                    client(home['dns'], home['endpoint']);work['tunnel'].probe()
                    ordinary_cli(home['scope'], 'connection', 'up', 'home-fixture')
                    for existing in contexts:existing['tunnel'].probe()
                    ordinary_cli(home['scope'], 'connection', 'down', 'home-fixture')
                    if tunnels == 'openvpn':home['tunnel'].wait_disconnected()
                home['service'].close()
                home['service'] = None
                require(fixture_connections(work['scope']) == ['work-fixture'], 'Stopping Home changed Work')
                work['tunnel'].probe() if tunnels else client(work['dns'], work['endpoint'])
                replacement_owner = JournalNetworkService.start(journal, home['dns'], state_root=root/'state')
                home['service'] = replacement_owner
                replacement = replacement_owner.scope
                home['scope'] = replacement
                require(replacement.unit.identity.pid != home_pid and replacement.state.identity == home_state,
                        'Worker restart did not retain its own persistent state')
                require(fixture_connections(replacement) == ['home-fixture'], 'Home did not reload its private connection')
                require(work['scope'].unit.identity == work_identity, 'Home restart replaced Work')
                if tunnels:
                    home['tunnel'].scope = replacement
                    ordinary_cli(replacement, 'connection', 'up', 'home-fixture')
                    for existing in contexts:existing['tunnel'].probe()
                else:
                    client(home['dns'], home['endpoint']);client(work['dns'], work['endpoint'])
                # systemd must retire the full plugin cgroup after the worker
                # dies; the journal then removes only its captured objects.
                from karyo_profiles.journal_dns import process_gone
                old = home['scope'].unit
                signal.pidfd_send_signal(old.pidfd, signal.SIGKILL)
                deadline = time.monotonic() + 7
                while True:
                    rows = old.info()
                    stopped = rows['LoadState'] == 'not-found' or (rows['MainPID'] == '0'
                              and rows['ActiveState'] in {'inactive', 'failed'} and not rows['ControlGroup'])
                    if (stopped and not os.path.lexists(home['scope'].directory) and not old.cgroup_pids()
                            and process_gone(old.identity.pid, old.identity.start)):break
                    require(time.monotonic() < deadline, 'The failed worker retained descendants or runtime files')
                    time.sleep(.05)
                home['service'].release_handles()
                retired = JournalNetworkService.restore(journal, home['dns'], state_root=root/'state')
                require(retired.scope.unit.closed, 'The failed worker was silently adopted as live')
                # Model a controller exit after removing the acknowledged
                # config file, before its directory/journal removal ACK.
                retired.update(pending='service-stop')
                retired.scope.unit.close()
                retired.update(pending='config-remove', unit=None)
                retired.scope.verify_config()
                os.unlink('NetworkManager.conf', dir_fd=retired.scope.config_fd)
                retired.release_handles()
                retired = JournalNetworkService.restore(journal, home['dns'], state_root=root/'state')
                retired.close();home['service'] = None
                replacement_owner = JournalNetworkService.start(journal, home['dns'], state_root=root/'state')
                home.update(service=replacement_owner, scope=replacement_owner.scope)
                if tunnels:
                    home['tunnel'].scope = home['scope']
                    ordinary_cli(home['scope'], 'connection', 'up', 'home-fixture')
                    for existing in contexts:existing['tunnel'].probe()
                else:
                    client(home['dns'], home['endpoint']);client(work['dns'], work['endpoint'])
                require(work['scope'].unit.identity == work_identity, 'Home failure replaced Work')
            evidence['two_private_instances_and_ordinary_clients'] = True
            evidence['restart_preserves_own_state_and_neighbor'] = True
            evidence['real_vpn_handshake'] = bool(tunnels)
            evidence['journaled_worker_failure_retirement_and_reactivation'] = True
            evidence['interrupted_configuration_retirement_resumed'] = True
            if tunnels:evidence['protocol'] = 'openvpn' if tunnels == 'openvpn' else 'wireguard'
            if tunnels:
                evidence['disconnect_reconnect_and_worker_restart_neighbor_payloads'] = True
                evidence['later_experiments_preserves_home_work_payloads'] = True
            retire_through_runtime(journal, store, root, contexts)
            evidence['generic_runtime_recovery_retired_all_workers'] = True
        finally:
            with store.locked():
                for value in reversed(contexts):
                    retired = value.get('retired_by_runtime', False)
                    service = None if retired else value['service']
                    if (not retired and service is None and value['dns'] is not None
                            and journal.record(value['network'].token).get('network_service', {}).get('pending') == 'uplink-prepare'):
                        service = JournalNetworkService.restore(journal, value['dns'], state_root=root/'state')
                    if service is not None:service.close()
                    if value['tunnel'] is not None:value['tunnel'].close()
                    if value['endpoint'] is not None:value['endpoint'].close()
                    if not retired:
                        if value['dns'] is not None:value['dns'].close()
                        value['network'].close()
                    if value['physical'] is not None:value['physical'].close()
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
