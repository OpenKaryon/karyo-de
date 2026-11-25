#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit preallocated graphical-profile bus acceptance on native QA copies.

Uses the installed unchanged entry/waiter and real compositor/shell. Physical
VT activation is a root fixture operation, not a family-switch acceptance claim.
"""
import grp
import json
import os
from pathlib import Path
import pwd
import secrets
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import DNSSeed, discover_dns
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS, recover_profiles
from karyo_profiles.journal_network import JournalNetwork, offline
from karyo_profiles.journal_network_service import JournalNetworkService
from karyo_profiles.journal_session import JournalSession, graphical_receipt
from karyo_profiles.journal_session import SessionRegistry
from karyo_profiles.graphical import GraphicalPending
from karyo_profiles.logind import Sessions
from karyo_profiles.networks import NetworkCommand
from karyo_profiles.provider_state import STATE_ROOT
from karyo_profiles.storage import ConfigStore
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.user_manager import ManagerPending
from karyo_profiles.host_network_policy import HostNetworkPolicy
from native_main_network import Endpoints, host_snapshot, require, run
from native_underlay_fixture import UnderlayAddress


def identity(path):
    node=Path(path).stat();return [node.st_dev,node.st_ino]


def program_pids(uid, name):
    result=[]
    for path in Path('/proc').iterdir():
        if not path.name.isdecimal():continue
        try:
            rows=(path/'status').read_text().splitlines()
            creds=next(line.split()[1:] for line in rows if line.startswith('Uid:'))
            if str(uid) in creds and Path(os.readlink(path/'exe')).name==name:result.append(int(path.name))
        except (FileNotFoundError,ProcessLookupError):pass
    return sorted(result)


def client(scope, endpoint):
    code='''import json,os,socket,subprocess,sys
from pathlib import Path
def identity(path):
 node=Path(path).stat();return [node.st_dev,node.st_ino]
assert identity('/proc/self/ns/net')==NET
assert identity('/run/systemd/resolve')==DNS
assert identity('/run/dbus/system_bus_socket')==BUS
assert {row[4][0] for row in socket.getaddrinfo(DOMAIN,None,socket.AF_INET)}=={ANSWER}
with socket.create_connection((PEER,TCP),timeout=3) as stream:assert stream.recv(64)==b'work'
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as stream:
 stream.settimeout(3);stream.sendto(b'payload',(PEER,UDP));assert stream.recv(64)==b'work'
for name,path in [('org.freedesktop.login1','/org/freedesktop/login1'),
                  ('org.freedesktop.PolicyKit1','/org/freedesktop/PolicyKit1/Authority'),
                  ('org.freedesktop.UDisks2','/org/freedesktop/UDisks2')]:
 result=subprocess.run(['/usr/bin/busctl','--system','--timeout=3s','call',name,path,
                        'org.freedesktop.DBus.Peer','Ping'],capture_output=True,text=True,timeout=5)
 assert result.returncode==0,result.stderr
private=os.environ.copy();private['DBUS_SYSTEM_BUS_ADDRESS']=PRIVATE
result=subprocess.run(['/usr/bin/nmcli','--wait','5','--terse','--fields','NAME','connection','show'],
                      env=private,capture_output=True,text=True,timeout=8)
assert result.returncode==0,result.stderr
evidence={'private_network_dns_host_bus':True,'dns_tcp_udp':True,
          'ordinary_logind_polkit_udisks':True,'stock_private_nmcli':True}
if CLIENTS:
 sys.path.insert(0,SOURCE)
 from karyo_profiles.network_client import command
 import secrets
 assert os.environ.get('KARYO_NETWORK_ENDPOINT')==ENDPOINT
 environment=dict(os.environ)
 name='karyo-client-'+secrets.token_hex(6)
 def invoke(*arguments):
  args,env=command('nmcli',list(arguments),environment)
  result=subprocess.run(args,env=env,capture_output=True,text=True,timeout=10)
  assert result.returncode==0,result.stderr
  return result.stdout
 invoke('--wait','5','connection','add','save','no','type','dummy','ifname','kc-client-check','con-name',name)
 try:
  assert name in invoke('--wait','5','--terse','--fields','NAME','connection','show').splitlines()
 finally:invoke('--wait','5','connection','delete',name)
 evidence['stock_client_adapter_private_add_show_delete']=True
 evidence['temporary_private_connection']=name
 args,env=command('connections',['--help'],environment)
 result=subprocess.run(args,env=env,capture_output=True,text=True,timeout=10)
 assert result.returncode==0,result.stderr
 evidence['stock_connection_editor_launcher_help']=True
print(json.dumps(evidence))
'''
    for key,value in dict(NET=list(scope.network.network.identity),
                         DNS=identity(scope.dns.files.plan.runtime_paths[0]),
                         BUS=(list(scope.host_bus.scope.socket_identity) if scope.host_bus is not None
                              else identity('/run/dbus/system_bus_socket')),DOMAIN=endpoint.domain,
                         ANSWER=endpoint.answer,PEER=endpoint.tcp.getsockname()[0],
                         TCP=endpoint.tcp.getsockname()[1],UDP=endpoint.udp.getsockname()[1],
                         PRIVATE=scope.dns.files.plan.bus_address,
                         SOURCE=str(Path(__file__).resolve().parents[1]),
                         ENDPOINT=str(scope.network_service.scope.directory/'client.json'),
                         CLIENTS=scope.network_service.scope.clients).items():
        # Only uppercase fixture constants occur as isolated tokens.
        import re
        code=re.sub(r'\b'+key+r'\b',lambda unused:repr(value),code)
    account=scope.manager.account
    result=subprocess.run(['/usr/sbin/runuser','-u',account.name,'--','/usr/bin/env',
        'XDG_RUNTIME_DIR=/run/user/'+str(account.uid),
        'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(account.uid)+'/bus',
        '/usr/bin/systemd-run','--user','--quiet','--wait','--pipe','--collect',
        '--unit=karyo-child-bus-probe-'+secrets.token_hex(6),'/usr/bin/python3','-I','-c',code],
        capture_output=True,text=True,timeout=30)
    require(result.returncode==0,'The real profile user-manager probe failed: '+result.stderr.strip())
    value=json.loads(result.stdout)
    if value.get('stock_client_adapter_private_add_show_delete'):
        host=run('/usr/bin/nmcli','--terse','--fields','NAME','connection','show').splitlines()
        require(value['temporary_private_connection'] not in host,'The client selected the host connection store')
    return value


def capture(scope,root):
    account=scope.manager.account
    result=subprocess.run(['/usr/sbin/runuser','-u',account.name,'--','/usr/bin/env',
        'XDG_RUNTIME_DIR=/run/user/'+str(account.uid),
        'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(account.uid)+'/bus',
        '/usr/bin/systemd-run','--user','--quiet','--wait','--pipe','--collect',
        '--unit=karyo-child-bus-capture-'+secrets.token_hex(6),'/usr/bin/grim','-'],
        capture_output=True,timeout=15)
    require(result.returncode==0 and result.stdout.startswith(b'\x89PNG'),
            'The real graphical fixture did not produce its screenshot')
    path=root/'desktop.png';path.write_bytes(result.stdout)
    return str(path)


def bus_identity(scope, *, root=False):
    """The unchanged system bus must see both the real UID and the real PID."""
    code='''import ctypes,json,os
lib=ctypes.CDLL('libsystemd.so.0');bus=ctypes.c_void_p();unique=ctypes.c_char_p()
lib.sd_bus_open_system.argtypes=[ctypes.POINTER(ctypes.c_void_p)]
assert lib.sd_bus_open_system(ctypes.byref(bus))>=0
lib.sd_bus_get_unique_name.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_char_p)]
assert lib.sd_bus_get_unique_name(bus,ctypes.byref(unique))>=0
lib.sd_bus_call_method.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,
                               ctypes.c_char_p,ctypes.c_void_p,ctypes.POINTER(ctypes.c_void_p),ctypes.c_char_p]
lib.sd_bus_message_read.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
lib.sd_bus_message_unref.argtypes=[ctypes.c_void_p];lib.sd_bus_message_unref.restype=ctypes.c_void_p
lib.sd_bus_unref.argtypes=[ctypes.c_void_p];lib.sd_bus_unref.restype=ctypes.c_void_p
values={}
for method,key in [(b'GetConnectionUnixUser','uid'),(b'GetConnectionUnixProcessID','pid')]:
 msg=ctypes.c_void_p();value=ctypes.c_uint()
 assert lib.sd_bus_call_method(bus,b'org.freedesktop.DBus',b'/org/freedesktop/DBus',b'org.freedesktop.DBus',
                              method,None,ctypes.byref(msg),b's',unique)>=0
 assert lib.sd_bus_message_read(msg,b'u',ctypes.byref(value))>=0
 values[key]=value.value;lib.sd_bus_message_unref(msg)
assert values['uid']==os.geteuid() and values['pid']==os.getpid(),values
print(json.dumps({'effective_uid':os.geteuid(),'bus_uid':values['uid'],'actual_pid_preserved':True}))
lib.sd_bus_unref(bus)
'''
    if root:
        args=['/usr/bin/systemd-run','--quiet','--wait','--pipe','--collect',
            '--unit=karyo-qa-root-bus-'+secrets.token_hex(6),'--property=User=root','--property=Group=root',
            *('--property='+value for value in scope.dns.files.plan.client_properties())]
    else:
        account=scope.manager.account
        args=['/usr/sbin/runuser','-u',account.name,'--','/usr/bin/env',
            'XDG_RUNTIME_DIR=/run/user/'+str(account.uid),
            'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(account.uid)+'/bus',
            '/usr/bin/systemd-run','--user','--quiet','--wait','--pipe','--collect',
            '--unit=karyo-qa-user-bus-'+secrets.token_hex(6)]
    result=subprocess.run([*args,'/usr/bin/python3','-I','-c',code],capture_output=True,text=True,timeout=10)
    require(result.returncode==0,'The real system-bus caller identity was changed: '+result.stderr)
    return json.loads(result.stdout)


def main():
    arguments=sys.argv[1:]
    direct='--direct-host-bus' in arguments
    if direct:arguments.remove('--direct-host-bus')
    mode=arguments[0] if arguments else 'live'
    require(os.getuid()==os.geteuid()==0 and len(arguments)<=1
            and mode in ('live','prepared','closing','policy-stop')
            and (mode!='policy-stop' or direct),'Use the root native fixture')
    require(socket.gethostname() in ('karyo-network-debian','karyo-network-ubuntu'),'Use the independent networking QA copies')
    command,accounts,sessions=NetworkCommand(),LocalAccounts(),Sessions()
    original=host_snapshot(command)
    original_seat=sessions.owner(sessions.seat_active())
    require(original_seat.kind=='greeter','The networking QA seat must start at its greeter')
    root=Path(tempfile.mkdtemp(prefix='child-bus-',dir='/var/lib/karyo-qa'))
    print('Graphical fixture state: '+str(root),flush=True)
    occupied={value.pw_uid for value in pwd.getpwall()} | {value.gr_gid for value in grp.getgrall()}
    uid=next(value for value in range(16000,16100) if value not in occupied and not (STATE_ROOT/str(value)).exists())
    name='karyo-graph-'+str(uid);home=Path('/home')/name
    require(not home.exists(),'The disposable graphical home already exists')
    run('/usr/sbin/useradd','-m','-U','-u',str(uid),'-s','/bin/bash',name)
    run('/usr/sbin/chpasswd',input=name+':'+secrets.token_urlsafe(32)+'\n')
    account=accounts.by_uid(uid);home_identity=identity(home)
    store=ConfigStore(root/'config',root/'no-legacy',root/'runtime')
    journal=RuntimeJournal(root/'journal')
    network=dns=service=scope=endpoint=address=policy=None
    evidence={'mode':mode,'preallocated_context_fixture':True,'production_family_switch':False,
              'real_graphical_login':False,
              'installed_current_runtime_package':False,'direct_host_bus':direct}
    try:
        with store.locked():
            network=JournalNetwork.prepare_main(journal,store,account,accounts=accounts,sessions=sessions,command=command)
            address=UnderlayAddress('192.0.2.30',network.token);address.create()
            endpoint=Endpoints(address.address,'work','198.18.0.2')
            seed=discover_dns(discover_underlay(command),command)
            dns=JournalDNS.start(journal,network,DNSSeed(seed.physical,(address.address,),seed.search),
                                 accounts=accounts,service_view='network-manager')
            service=JournalNetworkService.start(journal,dns)
            if direct:policy=HostNetworkPolicy.start(journal,dns)
            if mode=='prepared':
                from unittest.mock import patch
                from karyo_profiles.services import ServiceError
                with patch('karyo_profiles.journal_session.GraphicalSession.start',
                           side_effect=ServiceError('QA stops before graphical unit creation')):
                    try:JournalSession.start(journal,dns,host_bus=not direct)
                    except ServiceError:pass
                    else:raise RuntimeError('The controlled graphical preparation stop was ignored')
                record=journal.record(network.token)
                require(record['session']['pending']=='login-start'
                        and (record['host_network_policy']['pending'] is None if direct else record['host_bus']['unit'] is not None),
                        'The preparation lost its acknowledged bus selection and manager policy')
            else:scope=JournalSession.start(journal,dns,host_bus=not direct)
            service.release_handles();service=None
            evidence['manager_and_bus_selection_captured_before_graphical_entry']=True
            if not direct:evidence['manager_policy_and_proxy_captured_before_graphical_entry']=True
            if scope is not None:
                scope.verify_consumers()
                evidence['inactive_graphical_pam_and_manager_captured']=True
        if mode=='prepared':
            journal.close();journal=RuntimeJournal(root/'journal')
            registry=SessionRegistry(journal,store).restore()
            require(not registry.profiles and not registry.deferred and not journal.snapshot()['profiles'],
                    'Prepared-session recovery retained acknowledged fixture resources')
            registry.release_handles()
            evidence['prepared_manager_and_bus_selection_recovered_in_fresh_controller']=True
            if not direct:evidence['prepared_manager_proxy_recovered_in_fresh_controller']=True
        else:
            sessions.activate(scope.graph.identity.login.id)
            deadline=time.monotonic()+30
            while True:
                with store.locked():ready=scope.graph.compositor_ready()
                if ready and len(program_pids(uid,'karyo-shell'))==1:break
                require(time.monotonic()<deadline,'The real graphical profile did not become ready')
                time.sleep(.1)
            with store.locked():
                scope.graph.entered=True;scope.update(graphical=graphical_receipt(scope.graph))
                scope.verify_consumers()
                evidence['real_graphical_login']=True
                evidence['graphical_pid']=scope.graph.pid
                evidence['shell_pids']=program_pids(uid,'karyo-shell')
                expected=(scope.graph.identity,scope.manager.identity,
                          scope.host_bus.scope.unit.identity if scope.host_bus is not None else None,
                          scope.network_service.scope.unit.identity)
            evidence['ordinary_user_manager_client']=client(scope,endpoint)
            if direct:
                evidence['ordinary_user_identity']=bus_identity(scope)
                evidence['ordinary_root_identity']=bus_identity(scope,root=True)
                denied=subprocess.run(['/usr/sbin/runuser','-u',account.name,'--','/usr/bin/busctl','--system',
                    '--timeout=3s','get-property','org.freedesktop.NetworkManager','/org/freedesktop/NetworkManager',
                    'org.freedesktop.NetworkManager','State'],capture_output=True,text=True,timeout=5)
                require(denied.returncode!=0,'The ordinary client still selected the host network service')
                evidence['host_network_control_is_not_selected']=True
            if mode=='live':evidence['desktop_screenshot']=capture(scope,root)
            with store.locked():scope.verify_consumers()
            scope.release_handles();scope=None;journal.close();journal=RuntimeJournal(root/'journal')
            with store.locked():
                scope=JournalSession.restore(journal,store,journal.snapshot()['profiles'][0],accounts=accounts)
                if direct:
                    policy.release_handles()
                    policy=HostNetworkPolicy.restore(journal,scope.network.network,accounts=accounts,sessions=sessions)
                    policy.verify()
                actual=(scope.graph.identity,scope.manager.identity,
                        scope.host_bus.scope.unit.identity if scope.host_bus is not None else None,
                        scope.network_service.scope.unit.identity)
                require(actual==expected,'A fresh controller captured different graphical/profile worker lifetimes')
            evidence['same_graphical_manager_bus_selection_and_worker_after_reopen']=True
            if not direct:evidence['same_graphical_manager_proxy_network_worker_after_reopen']=True
            evidence['client_after_controller_reopen']=client(scope,endpoint)
            if mode=='policy-stop':
                from unittest.mock import patch
                deadline=time.monotonic()+35
                while True:
                    try:
                        with store.locked(),patch.object(HostNetworkPolicy,'reload',side_effect=RuntimeError('QA pauses policy reload')):
                            scope.close()
                    except (GraphicalPending,ManagerPending):
                        require(time.monotonic()<deadline,'The profile did not finish naturally before the policy reload pause')
                        time.sleep(.1)
                    except RuntimeError as error:
                        require(str(error)=='QA pauses policy reload','An unexpected policy close error occurred')
                        break
                    else:raise RuntimeError('The controlled policy reload pause was ignored')
                record=journal.record(network.token)
                require('session' not in record and record['host_network_policy']['pending']=='remove',
                        'Policy retirement lost its recorded removal intent')
                scope.release_handles();scope=None;journal.close();journal=RuntimeJournal(root/'journal')
                recover_profiles(journal,store,accounts=accounts,sessions=sessions)
                require(not journal.snapshot()['profiles'],'Fresh recovery retained the completed policy retirement')
                evidence['interrupted_policy_removal_reloaded_in_fresh_controller']=True
            if mode=='closing':
                with store.locked():
                    scope.graph.close()
                    scope.update(pending='manager-close',graphical=None)
                    if scope.host_bus is not None:scope.host_bus.update(pending='stop')
                scope.release_handles();scope=None;journal.close();journal=RuntimeJournal(root/'journal')
                registry=SessionRegistry(journal,store).restore()
                deadline=time.monotonic()+35
                while registry.profiles or registry.deferred:
                    registry.poll()
                    require(time.monotonic()<deadline,'Closing-session recovery did not finish naturally')
                    time.sleep(.1)
                require(not journal.snapshot()['profiles'],'Closing recovery retained fixture receipts')
                registry.release_handles()
                evidence['closing_session_recovered_in_fresh_controller']=True
                if not direct:evidence['interrupted_proxy_retirement_intent_resumed']=True
    except BaseException as error:
        (root/'failure.txt').write_text(type(error).__name__+': '+str(error)+'\n')
        raise
    finally:
        if scope is not None:
            deadline=time.monotonic()+35
            while True:
                try:
                    with store.locked():scope.close()
                    break
                except (GraphicalPending,ManagerPending):
                    require(time.monotonic()<deadline,'The captured graphical profile did not finish naturally')
                    time.sleep(.15)
            scope.release_handles();scope=None
        # An unacknowledged graphical start is retained for review. Never remove
        # accounts or network state while such a journal receipt remains.
        if not any('session' in value for value in journal.snapshot()['profiles']):
            if policy is not None:policy.release_handles();policy=None
            if service is not None:service.release_handles();service=None
            if dns is not None:dns.release_handles()
            if network is not None and network.network.fd is not None:
                os.close(network.network.fd);network.network.fd=None
            journal.close();journal=RuntimeJournal(root/'journal')
            recover_profiles(journal,store,accounts=accounts,sessions=sessions)
            require(not journal.snapshot()['profiles'],'The graphical fixture retained runtime resources')
            if direct:
                from karyo_profiles.host_network_policy import DIRECTORY
                require(not (DIRECTORY/(network.token+'.conf')).exists()
                        and not (DIRECTORY/(network.token+'.pending')).exists(),'The profile UID policy remained active')
                evidence['uid_policy_removed_before_account_retirement']=True
            if endpoint is not None:endpoint.close()
            if address is not None:address.close()
            offline(account,accounts,sessions)
            require(identity(home)==home_identity,'The disposable home changed')
            run('/usr/sbin/userdel','-r',name)
        if original_seat.id in sessions._identifiers():
            require(sessions.owner(original_seat.id).identity==original_seat.identity,'The original QA greeter changed')
            sessions.activate(original_seat.id)
        journal.close()
    require(host_snapshot(command)==original,'The graphical fixture changed its host baseline')
    evidence['natural_manager_exit_owned_cleanup_and_host_baseline']=True
    finish(root,evidence)


def finish(root,evidence):
    (root/'result.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(json.dumps(evidence,indent=2))


if __name__=='__main__':main()
