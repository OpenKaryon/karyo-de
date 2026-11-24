#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit real-PAM handoff using durable main-entry ownership.

The production dispatcher and main registry receive a locally injected root
harness and preallocated fixture context. Installed host/PAM stacks remain
unchanged; the temporary PAM service authenticates only the temporary account.
"""
import json
import os
from pathlib import Path
import pwd
import grp
import secrets
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dispatcher import HostDispatcher
from karyo_profiles.dns import DNSSeed, discover_dns, supported_resolver_layout
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_network import JournalNetwork, offline
from karyo_profiles.journal_main import MainEntry
from karyo_profiles.journal_network_service import JournalNetworkService
from karyo_profiles.main_registry import MainRegistry
from karyo_profiles.main_dispatcher import MainDispatcher
from karyo_profiles.logind import Sessions
from karyo_profiles.networks import NetworkCommand
from karyo_profiles.services import program_identity
from karyo_profiles.provider_state import STATE_ROOT
from karyo_profiles.storage import ConfigStore, trusted_directory
from karyo_profiles.underlay import discover_underlay
from native_main_network import Endpoints, require, run, host_snapshot
from native_underlay_fixture import UnderlayAddress


def identity(path):
    value = Path(path).stat()
    return value.st_dev, value.st_ino


def main():
    arguments = sys.argv[1:]
    network_service_mode = '--network-service' in arguments
    if network_service_mode:arguments.remove('--network-service')
    require(os.getuid() == os.geteuid() == 0 and len(arguments) in (1, 2), 'Root QA build directory required')
    require(socket.gethostname() in ('karyo-network-debian', 'karyo-network-ubuntu'), 'Use networking QA copies')
    build = Path(arguments[0]).resolve()
    module = Path(arguments[1]).resolve() if len(arguments) == 2 else build/'pam_karyo_network.so'
    harness = build/'karyo-native-pam-harness'
    require(module.is_file() and harness.is_file(), 'Build the native PAM component first')
    program_identity(str(harness))
    pam_file = Path('/etc/pam.d/karyo-network-qa')
    address = Path('/run/karyo/profiles.sock')
    require(not os.path.lexists(pam_file) and not os.path.lexists(address), 'Existing PAM/controller endpoint retained')
    parent = Path('/var/lib/karyo-qa')
    parent.mkdir(mode=0o755, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='main-pam-', dir=parent))
    print('PAM fixture state: ' + str(root), flush=True)
    command = NetworkCommand()
    before = host_snapshot(command)
    supported_resolver_layout()
    seed = discover_dns(discover_underlay(command), command)
    occupied = {value.pw_uid for value in pwd.getpwall()} | {value.gr_gid for value in grp.getgrall()}
    uid = next(value for value in range(16000, 16100)
               if value not in occupied and not (STATE_ROOT/str(value)).exists())
    name = 'karyo-main-' + str(uid)
    home = Path('/home')/name
    require(not home.exists(), 'Fixture account home exists')
    run('/usr/sbin/useradd', '-m', '-U', '-u', str(uid), '-s', '/bin/bash', name)
    accounts, sessions = LocalAccounts(), Sessions()
    account = accounts.by_uid(uid)
    home_identity = identity(home)
    password = secrets.token_urlsafe(32)
    run('/usr/sbin/chpasswd', input=name + ':' + password + '\n')
    store = ConfigStore(root/'config', root/'no-legacy', root/'runtime')
    unit = 'karyo-main-pam-' + secrets.token_hex(8)
    state = {'prepared': False, 'attached': False, 'closing': False, 'errors': []}
    network = dns = manager = entry = endpoint = listener = thread = None
    initial_service = physical_address = None
    finished = threading.Event()
    pam_contents = ('auth required pam_unix.so\naccount required pam_unix.so\n'
                    'session requisite ' + str(module) + ' prepare desktop=karyo\n'
                    'session required pam_systemd.so type=wayland class=user\n'
                    'session requisite ' + str(module) + ' attach desktop=karyo\n')
    with RuntimeJournal(root/'journal') as journal:
        try:
            with store.locked():
                network = JournalNetwork.prepare_main(journal, store, account,
                                                      accounts=accounts, sessions=sessions, command=command)
                if network_service_mode:
                    physical_address = UnderlayAddress('192.0.2.20', network.token)
                    physical_address.create()
                    endpoint = Endpoints(physical_address.address, 'home', '198.18.0.1')
                    physical_seed = DNSSeed(seed.physical, (physical_address.address,), seed.search)
                    dns = JournalDNS.start(journal, network, physical_seed, accounts=accounts,
                                           service_view='network-manager')
                    initial_service = JournalNetworkService.start(journal, dns)
                    state['worker_invocation'] = initial_service.scope.unit.identity.invocation
                else:
                    dns = JournalDNS.start(journal, network, seed, accounts=accounts)
                    endpoint = Endpoints(network.network.spec.host, 'home', '198.18.0.1')
                    command.run('/usr/bin/busctl', '--address=' + dns.files.plan.bus_address, '--timeout=3s',
                                'call', 'org.freedesktop.resolve1', '/org/freedesktop/resolve1',
                                'org.freedesktop.resolve1.Manager', 'SetLinkDNS', 'ia(iay)',
                                str(network.network.indices[1]), '1', '2', '4', *network.network.spec.host.split('.'))
            directory = trusted_directory(address.parent, create=True)
            os.close(directory)
            listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            listener.bind(str(address)); address.chmod(0o600)
            socket_identity = identity(address)
            listener.listen(4); listener.settimeout(.2)
            fd = os.open(pam_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o644)
            with os.fdopen(fd, 'w') as stream:
                stream.write(pam_contents)
            pam_identity = identity(pam_file)

            registry = MainRegistry(journal, store)

            def fixture_prepare(selected_registry, selected, peer, selected_accounts, selected_sessions):
                require(selected == account, 'Fixture account changed')
                prepared = MainEntry.prepare(selected_registry.journal, dns, peer)
                if initial_service is not None:initial_service.release_handles()
                return prepared

            login_dispatcher = MainDispatcher(registry, accounts, sessions, users=lambda: (name,),
                                              prepare=fixture_prepare, program=str(harness),
                                              cgroup='/system.slice/' + unit + '.service', services=('karyo-network-qa',))
            dispatcher = HostDispatcher(store, accounts=accounts, sessions=sessions, login=login_dispatcher)

            def handle(connection):
                nonlocal journal, network, dns, manager, entry, registry
                dispatcher.handle(connection)
                scopes = list(registry.entries.values())
                require(len(scopes) == 1, 'Main dispatcher did not retain its captured entry')
                entry = scopes[0]
                value = journal.record(entry.token)['main_login']
                if value['pending'] == 'entry-start' and not state['prepared']:
                    state['prepared'] = True
                    restored = 'restored_before_entry'
                elif value['pending'] is None and not state['attached']:
                    state['attached'] = True
                    restored = 'restored_after_attach'
                elif value['pending'] == 'login-close':
                    state['closing'] = True
                    return
                else:
                    raise RuntimeError('Unexpected main entry transition')
                # Recreate registry/observers from the reopened durable journal.
                registry.release_handles()
                journal.close()
                journal = RuntimeJournal(root/'journal')
                registry = MainRegistry(journal, store).restore()
                login_dispatcher.registry = registry
                entry = next(iter(registry.entries.values()))
                network, dns, manager = entry.network, entry.dns, entry.manager
                if network_service_mode:
                    require(entry.network_service.scope.unit.identity.invocation == state['worker_invocation'],
                            'Main recovery changed the captured network worker')
                if state['attached']:
                    with store.locked():entry.verify_consumers()
                    (home/'.karyo_entry_ready').touch(mode=0o644)
                state[restored] = True

            def serve():
                while not finished.is_set():
                    try: connection, _ = listener.accept()
                    except socket.timeout: continue
                    with connection:
                        try: handle(connection)
                        except BaseException as error:
                            state['errors'].append(type(error).__name__ + ': ' + str(error))
                            connection.sendall(struct.pack('!III', 0x4b4e4554, 1, 0))

            thread = threading.Thread(target=serve, daemon=True); thread.start()
            probe = """import json,os,socket
from pathlib import Path
def identity(path):
 value=Path(path).stat();return [value.st_dev,value.st_ino]
assert identity('/proc/self/ns/net')==EXPECTED_NET
assert identity('/run/systemd/resolve')==EXPECTED_DNS
assert {value[4][0] for value in socket.getaddrinfo('home.context.test',None,socket.AF_INET)}=={'198.18.0.1'}
with socket.create_connection((HOST,TCP),timeout=3) as stream: assert stream.recv(64)==b'home'
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as stream:
 stream.settimeout(3);stream.sendto(b'payload',(HOST,UDP));assert stream.recv(64)==b'home'
print(json.dumps({'network':True,'resolver_mount':True,'dns':True,'tcp':True,'udp':True}))
""".replace('EXPECTED_NET', repr(list(network.network.identity))).replace('EXPECTED_DNS', repr(list(identity(dns.files.plan.runtime_paths[0]))))
            probe = probe.replace('HOST', repr(endpoint.tcp.getsockname()[0])).replace('TCP', str(endpoint.tcp.getsockname()[1])).replace('UDP', str(endpoint.udp.getsockname()[1]))
            client_code = """import time
from pathlib import Path
deadline=time.monotonic()+15
while not Path(READY).is_file():
 assert time.monotonic()<deadline,'Main controller did not finish restoring observers'
 time.sleep(.05)
""".replace('READY', repr(str(home/'.karyo_entry_ready'))) + probe + """
import subprocess
os.environ['DBUS_SESSION_BUS_ADDRESS']='unix:path=/run/user/'+str(os.getuid())+'/bus'
result=subprocess.run(['/usr/bin/systemd-run','--user','--quiet','--wait','--pipe','--collect','--unit=karyo-main-user-probe','/usr/bin/python3','-I','-c',PROBE],capture_output=True,text=True,timeout=15)
assert result.returncode==0,result.stderr
assert json.loads(result.stdout)['network']
print(json.dumps({'user_manager_activation_same_context':True}))
""".replace('PROBE', repr(probe))
            result = subprocess.run(['/usr/bin/systemd-run', '--quiet', '--wait', '--pipe', '--collect',
                                     '--unit=' + unit, '--property=Type=exec', str(harness), name, client_code],
                                    input=password + '\n', text=True, capture_output=True, timeout=80)
            password = None
            (root/'harness.log').write_text(result.stdout + result.stderr)
            require(result.returncode == 0, 'Native PAM harness failed; inspect its retained log')
            require(not state['errors'] and all(state[field] for field in ('prepared','attached','closing')),
                    'PAM lifecycle did not complete: ' + repr(state['errors']))
            evidence = {'real_pam_authentication_and_session': True,
                        'namespace_and_dns_before_user_manager': True,
                        'ordinary_user_dns_tcp_udp': True, 'user_manager_activation_same_context': True,
                        'parent_context_restored': '"privileged_parent_context_restored":true' in result.stdout,
                        'production_main_entry_lifecycle': True,
                        'journal_reopened_before_entry': state['restored_before_entry'],
                        'journal_reopened_after_attach': state['restored_after_attach'],
                        'production_dispatcher_and_registry': True,
                        'preallocated_context_fixture': True, 'real_graphical_login': False}
            if network_service_mode:
                evidence['private_network_worker_with_real_pam'] = True
                evidence['same_worker_across_controller_restoration'] = True
            require(evidence['parent_context_restored'], 'PAM parent did not restore its original context')
        finally:
            (root/'controller_state.json').write_text(json.dumps({key:value for key,value in state.items() if key!='pidfd'}, indent=2) + '\n')
            finished.set()
            if thread is not None:
                thread.join(timeout=3); require(not thread.is_alive(), 'Fixture controller did not stop')
            if listener is not None:
                listener.close()
                require(identity(address) == socket_identity, 'Fixture socket changed')
                address.unlink()
            if pam_file.exists():
                require(identity(pam_file) == pam_identity and pam_file.read_text() == pam_contents, 'Fixture PAM file changed')
                pam_file.unlink()
            if endpoint is not None: endpoint.close()
            if entry is not None:
                deadline = time.monotonic() + 25
                while not manager.inactive(manager.info()):
                    require(time.monotonic() < deadline, 'Main manager has not finished naturally')
                    time.sleep(.2)
                registry.release_handles()
                journal.close()
                journal = RuntimeJournal(root/'journal')
                registry = MainRegistry(journal, store).restore()
                registry.poll()
                require(not registry.tokens, 'Main registry retained a completed fixture lifetime')
                state['restored_after_exit'] = True
                network = dns = manager = entry = None
            else:
                with store.locked():
                    if initial_service is not None:
                        initial_service.release_handles()
                        JournalNetworkService.restore(journal, dns).close()
                    if dns is not None:dns.close()
                    if network is not None:network.close()
            if physical_address is not None:physical_address.close()
            offline(account, accounts, sessions)
            require(identity(home) == home_identity, 'Fixture home changed')
            run('/usr/sbin/userdel', '-r', name)
        require(not journal.snapshot()['profiles'] and host_snapshot(command) == before, 'PAM fixture did not restore baseline')
        evidence['captured_cleanup_and_host_baseline'] = True
        evidence['journal_reopened_after_parent_and_manager_exit'] = state.get('restored_after_exit', False)
        (root/'result.json').write_text(json.dumps(evidence, indent=2) + '\n')
        print(json.dumps(evidence, indent=2))
        journal.close()


if __name__ == '__main__':
    main()
