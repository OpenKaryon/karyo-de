#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary host-service calls and private nmcli through stock bus projections."""
import json
import os
import secrets
import subprocess

from native_main_network import require


def probe_host_services(scope, label, endpoint):
    scope.verify()
    code = '''import json,os,socket,subprocess
def call(*args, environment=None):
 result=subprocess.run(['/usr/bin/busctl','--system','--json=short','--timeout=3s',*args],
                       env=environment,capture_output=True,text=True,timeout=5)
 assert result.returncode==0,result.stderr
 return json.loads(result.stdout) if result.stdout.strip() else None
names=call('call','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','ListNames')['data'][0]
assert 'org.freedesktop.login1' in names
assert not {'org.freedesktop.NetworkManager','org.freedesktop.resolve1','org.freedesktop.network1'} & set(names)
for name,path in [('org.freedesktop.login1','/org/freedesktop/login1'),
                  ('org.freedesktop.PolicyKit1','/org/freedesktop/PolicyKit1/Authority'),
                  ('org.freedesktop.UDisks2','/org/freedesktop/UDisks2'),
                  ('org.freedesktop.systemd1','/org/freedesktop/systemd1')]:
 call('call',name,path,'org.freedesktop.DBus.Peer','Ping')
actions=call('call','org.freedesktop.PolicyKit1','/org/freedesktop/PolicyKit1/Authority',
             'org.freedesktop.PolicyKit1.Authority','EnumerateActions','s','')
assert actions['data']
devices=call('call','org.freedesktop.UDisks2','/org/freedesktop/UDisks2',
             'org.freedesktop.DBus.ObjectManager','GetManagedObjects')
assert devices['data']
fallback=os.environ.copy();fallback.pop('DBUS_SYSTEM_BUS_ADDRESS',None)
call('call','org.freedesktop.login1','/org/freedesktop/login1','org.freedesktop.DBus.Peer','Ping',environment=fallback)
node=os.stat('/run/dbus/system_bus_socket')
assert [node.st_dev,node.st_ino]==EXPECTED_SOCKET
node=os.stat('/run/systemd/resolve')
assert [node.st_dev,node.st_ino]==EXPECTED_RESOLVER
assert {row[4][0] for row in socket.getaddrinfo(DOMAIN,None,socket.AF_INET)}=={ANSWER}
with socket.create_connection((PEER,TCP),timeout=3) as stream:assert stream.recv(64)==PAYLOAD
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as stream:
 stream.settimeout(3);stream.sendto(b'payload',(PEER,UDP));assert stream.recv(64)==PAYLOAD
private=os.environ.copy();private['DBUS_SYSTEM_BUS_ADDRESS']=PRIVATE_BUS
result=subprocess.run(['/usr/bin/nmcli','--wait','5','--terse','--fields','NAME','connection','show'],
                      env=private,capture_output=True,text=True,timeout=10)
assert result.returncode==0,result.stderr
assert [name for name in result.stdout.splitlines() if name.endswith('-fixture')]==[EXPECTED_NAME]
print(json.dumps({'ordinary_host_logind_polkit_udisks_systemd':True,
                 'polkit_actions_and_disk_inventory':True,'standard_socket_same_proxy':True,
                 'default_service_inventory_omits_host_network_daemons':True,
                 'projected_client_private_dns_tcp_udp':True,
                 'stock_nmcli_selects_own_connection_store':True}))
'''
    resolver = os.stat(scope.files.plan.runtime_paths[0])
    replacements = dict(EXPECTED_SOCKET=list(scope.socket_identity),
                        EXPECTED_RESOLVER=[resolver.st_dev,resolver.st_ino],
                        PRIVATE_BUS=scope.files.plan.bus_address, EXPECTED_NAME=label+'-fixture',
                        DOMAIN=endpoint.domain, ANSWER=endpoint.answer, PEER=endpoint.tcp.getsockname()[0],
                        TCP=endpoint.tcp.getsockname()[1], UDP=endpoint.udp.getsockname()[1], PAYLOAD=label.encode())
    for key,value in replacements.items():code=code.replace(key,repr(value))
    options = ['/usr/bin/systemd-run', '--quiet', '--wait', '--pipe', '--collect',
               '--unit=karyo-host-bus-client-' + secrets.token_hex(8),
               '--property=User=' + str(scope.network.spec.uid), '--property=Group=' + str(scope.files.plan.gid),
               *('--property=' + value for value in scope.client_properties())]
    result = subprocess.run([*options, '/usr/bin/python3', '-I', '-c', code], capture_output=True,
                            text=True, timeout=30)
    require(result.returncode == 0, 'Ordinary projected bus client failed: ' + result.stderr.strip())
    scope.verify()
    evidence = json.loads(result.stdout)
    evidence.update(proxy_pid=scope.unit.identity.pid, proxy_uid=scope.unit.uid,
                    proxy_effective_and_bounding_capabilities_zero=True)
    return evidence


if __name__ == '__main__':
    from native_network_manager import main
    main(host_bus=True)
