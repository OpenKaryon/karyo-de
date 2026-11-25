#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe a prepared stock VPN browser/GUI flow in an own temporary desktop.

Run after ordinary browser authentication and native tray interactions. This
fixture neither supplies credentials nor treats saved QA metadata as authority
to launch a production worker. The installed controller still owns all actors.
"""
from pathlib import Path
import ast
import json
import os
import pwd
import socket
import subprocess
import sys
import time

assert os.getuid() == 0
assert socket.gethostname() in ('karyo-network-debian', 'karyo-network-ubuntu')
root = Path(Path('/run/karyo-qa-main/root').read_text().strip())
proof = json.loads((root / 'browser-auth-desktop.json').read_text())
rows = json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles']
third = next(row for row in rows if row['token'] == proof['third_token'])
assert third == proof['third_record']
assert len(rows) == 3
for before in proof['baseline_records']:
    assert next(row for row in rows if row['token'] == before['token']) == before
uid = third['account']['uid']
account = pwd.getpwuid(uid)
gui = proof['vendor_gui']
proc = Path('/proc') / str(gui['pid'])
assert proc.stat().st_uid == uid
assert (proc / 'exe').resolve() == Path('/usr/bin/netbird-ui')
assert (proc / 'stat').read_text().rpartition(') ')[2].split()[19] == gui['start']
assert (proc / 'cgroup').read_text().strip() == gui['cgroup']
assert 'CapEff:\t0000000000000000' in (proc / 'status').read_text()

def identity(path):
    info = path.stat()
    return [info.st_dev, info.st_ino]

assert identity(proc / 'ns/net') == third['network']['namespace']
assert identity(proc / 'root/run/systemd/resolve') == third['dns']['resolver']['runtime'][0]
environment = dict(value.split('=', 1) for value in
                   (proc / 'environ').read_bytes().decode().split('\0') if '=' in value)
assert environment['NB_DAEMON_ADDR'] == ('unix:///run/karyo-netbird-' + third['token'] + '/control.sock')
actors = []
for candidate in Path('/proc').iterdir():
    if not candidate.name.isdecimal():
        continue
    try:
        if candidate.stat().st_uid == uid and (candidate / 'exe').resolve() == Path('/usr/bin/netbird-ui'):
            actors.append(int(candidate.name))
    except (FileNotFoundError, ProcessLookupError):
        pass
assert actors == [gui['pid']], actors
peer = json.loads((root / 'browser-auth-peer.json').read_text())
client = r'''from pathlib import Path
import hashlib,json,os,socket,subprocess,urllib.request
def identity(path):
 info=Path(path).stat();return [info.st_dev,info.st_ino]
assert identity('/proc/self/ns/net')==NETWORK
assert identity('/run/systemd/resolve')==RESOLVER
status=json.loads(subprocess.check_output(['netbird','status','--json'],text=True,timeout=12))
assert status['management']['connected'] and status['signal']['connected']
assert {v[4][0] for v in socket.getaddrinfo(PEER['fqdn'],PEER['port'],socket.AF_INET)}=={PEER['ip']}
data=os.urandom(512);expected=PEER['host'].encode()+b':'+hashlib.sha256(data).digest()
with socket.create_connection((PEER['ip'],PEER['port']),timeout=6) as stream:
 stream.sendall(data);answer=b''
 while len(answer)<len(expected):
  block=stream.recv(1024);assert block;answer+=block
 assert answer==expected
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
 udp.settimeout(6);udp.sendto(data,(PEER['ip'],PEER['port']));assert udp.recv(1024)==expected
assert json.loads(subprocess.check_output(['ip','-j','-4','route','get',PEER['ip']],text=True))[0]['dev']=='wt0'
assert socket.getaddrinfo('example.com',443,socket.AF_INET)
with urllib.request.urlopen('https://example.com',timeout=10) as response:assert response.status==200
print(json.dumps({'management_signal_connected':True,'managed_dns_encrypted_tcp_udp':True,'external_dns_verified_https':True}))
'''
client_template = client
values = {'NETWORK': third['network']['namespace'], 'RESOLVER': third['dns']['resolver']['runtime'][0], 'PEER': peer}
client = '\n'.join(key + '=' + repr(value) for key, value in values.items()) + '\n' + client
ast.parse(client)
prefix = ['setpriv', '--reuid=' + str(uid), '--regid=' + str(account.pw_gid),
          '--init-groups', '--inh-caps=-all', '--ambient-caps=-all', 'env',
          'XDG_RUNTIME_DIR=/run/user/' + str(uid),
          'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/' + str(uid) + '/bus']
result = subprocess.run([*prefix, 'systemd-run', '--user', '--quiet', '--wait', '--pipe',
                         '--collect', '--unit=karyo-qa-browser-gui-probe',
                         '/usr/bin/python3', '-I', '-c', client], capture_output=True, text=True, timeout=40)
assert result.returncode == 0, result.stderr[-1800:]
assert json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles'] == rows
answer = json.loads(result.stdout)
answer.update(single_captured_vendor_gui=True, gui_private_network_resolver=True,
              private_control_socket=True, gui_effective_capabilities_empty=True,
              home_work_entire_receipts_unchanged=True)
assert sys.argv[1:] in ([], ['--retire'])
if sys.argv[1:] == ['--retire']:
    shells = []
    for candidate in Path('/proc').iterdir():
        if not candidate.name.isdecimal():
            continue
        try:
            if candidate.stat().st_uid == uid and (candidate / 'exe').resolve() == Path('/usr/bin/karyo-shell'):
                shells.append(candidate)
        except (FileNotFoundError, ProcessLookupError):
            pass
    assert len(shells) == 1
    shell_env = dict(value.split('=', 1) for value in
                     (shells[0] / 'environ').read_bytes().decode().split('\0') if '=' in value)
    compositor = Path('/proc') / shell_env['LABWC_PID']
    assert compositor.stat().st_uid == uid and (compositor / 'exe').resolve() == Path('/usr/bin/labwc')
    subprocess.run([*prefix, 'systemd-run', '--user', '--quiet', '--collect',
                    '--unit=karyo-qa-browser-logout', '--setenv=LABWC_PID=' + compositor.name,
                    '/usr/bin/labwc', '--exit'], check=True, timeout=10)
    deadline = time.monotonic() + 45
    while True:
        remaining = json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles']
        if all(row['token'] != third['token'] for row in remaining):
            break
        assert time.monotonic() < deadline, 'The authenticated desktop did not retire'
        time.sleep(.2)
    assert len(remaining) == 2
    for before in proof['baseline_records']:
        assert next(row for row in remaining if row['token'] == before['token']) == before
    token = third['token']
    for name in ('netns/karyo-', 'karyo-netbird-', 'karyo-network-manager-',
                 'karyo-resolve-', 'karyo-resolve-bus-', 'karyo-host-bus-'):
        assert not os.path.lexists('/run/' + name + token)
    for old in (third['provider']['service']['unit'], third['dns']['resolver'],
                third['dns']['bus'], gui):
        old_proc = Path('/proc') / str(old['pid'])
        assert not old_proc.exists() or (old_proc / 'stat').read_text().rpartition(') ')[2].split()[19] != str(old['start'])
    for before in proof['baseline_records']:
        sibling_uid = before['account']['uid']
        sibling = pwd.getpwuid(sibling_uid)
        sibling_prefix = ['setpriv', '--reuid=' + str(sibling_uid), '--regid=' + str(sibling.pw_gid),
                          '--init-groups', '--inh-caps=-all', '--ambient-caps=-all', 'env',
                          'XDG_RUNTIME_DIR=/run/user/' + str(sibling_uid),
                          'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/' + str(sibling_uid) + '/bus']
        values = {'NETWORK': before['network']['namespace'],
                  'RESOLVER': before['dns']['resolver']['runtime'][0], 'PEER': peer}
        sibling_client = '\n'.join(key + '=' + repr(value) for key, value in values.items()) + '\n' + client_template
        result = subprocess.run([*sibling_prefix, 'systemd-run', '--user', '--quiet', '--wait', '--pipe',
                                 '--collect', '--unit=karyo-qa-after-browser-logout',
                                 '/usr/bin/python3', '-I', '-c', sibling_client],
                                capture_output=True, text=True, timeout=40)
        assert result.returncode == 0, result.stderr[-1800:]
    assert json.loads(Path('/run/karyo/profile-host/state.json').read_text())['profiles'] == remaining
    answer.update(natural_authenticated_desktop_logout=True,
                  captured_worker_gui_dns_retired=True, private_runtime_paths_retired=True,
                  home_work_dns_encrypted_tcp_udp_after_logout=True)
(root / 'browser-gui-result.json').write_text(json.dumps(answer, indent=2) + '\n')
print(json.dumps(answer, indent=2))
