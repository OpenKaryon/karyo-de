# SPDX-License-Identifier: GPL-3.0-or-later
"""Own native QA placement probe; not a production activation implementation."""
import json
import os
from pathlib import Path
import socket
import struct
import uuid

from karyo_profiles.worker_declaration import DIRECTORY, InstalledDeclaration
from karyo_profiles.worker_entry import process_start
from karyo_profiles.worker_files import WorkerFiles
from karyo_profiles.worker_origin import node
from karyo_profiles.worker_unit import DeclaredUnit, ENTRY


def probe(context):
    assert socket.gethostname() in ('karyo-network-debian', 'karyo-network-ubuntu')
    assert os.getuid() == 0
    folder = Path(__file__).resolve().parent
    entry = Path(ENTRY)
    declaration_path = DIRECTORY/'org.karyo.Fixture.json'
    assert not os.path.lexists(entry) and not os.path.lexists(declaration_path)
    DIRECTORY.mkdir(mode=0o755, parents=True, exist_ok=True)
    entry_data = ('#!/usr/bin/python3 -I\nimport sys\nsys.path.insert(0,' +
                  repr(str(folder.parent)) + ')\nfrom karyo_profiles.worker_entry import main\n'
                  'raise SystemExit(main())\n').encode()
    entry.write_bytes(entry_data)
    entry.chmod(0o755)
    declaration = {'version': 1, 'id': 'org.karyo.Fixture', 'revision': '1.0-1',
        'argv': ['/usr/bin/python3', '-I', str(folder/'native_worker_fixture.py')],
        'capabilities': ['CAP_NET_ADMIN', 'CAP_NET_RAW'], 'ready_seconds': 5,
        'cleanup_seconds': 10, 'stop_application': True}
    declaration_data = (json.dumps(declaration)+'\n').encode()
    declaration_path.write_bytes(declaration_data)
    declaration_path.chmod(0o644)
    instance = uuid.uuid4().hex
    intent = folder/('intent-'+instance+'.json')
    intent.write_text(json.dumps({'instance':instance,'desktop':context.desktop_token,
        'unit':'karyo-desktop-worker-'+instance+'.service','owner':os.getpid(),
        'source':str(folder),'phase':'before-files'})+'\n')
    intent.chmod(0o600)
    files = unit = None
    completed = False
    try:
        with context.scope.network.network.store.locked():
            context.verify()
            installed = InstalledDeclaration.capture('org.karyo.Fixture')
            files = WorkerFiles(context, instance).prepare()
            plan = context.scope.dns.files.plan
            value = {'version': 1, 'role': 'managed-worker-bootstrap',
                'owner': {'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                          'pid': os.getpid(), 'start': process_start(os.getpid()), 'epoch': uuid.uuid4().hex},
                'gui': {'uid':context.origin.identity.uid,'gid':context.origin.identity.gid},
                'desktop': {'id': context.desktop_token, 'generation': 1},
                'worker': {'id': instance, 'generation': 1,
                           'unit': 'karyo-desktop-worker-'+instance+'.service'},
                'declaration': {'id': installed.declaration.identifier,
                                'revision': installed.declaration.revision, 'sha256': installed.digest},
                'context': {'network': list(context.scope.network.network.identity),
                    'user': list(context.origin.identity.user),
                    'resolver': {'directory': list(context.resolver_node)},
                    'bus': {'address': plan.bus_address, 'socket': list(context.resolver_bus_node)}},
                'directories': {role: {'path': str(path), 'node': list(node(path))}
                                for role, path in (('runtime', files.runtime), ('state', files.state))}}
            files.publish_prepared(value)
            unit = DeclaredUnit(context, installed, instance)
            baseline = context.scope.network.network._ip('-j', 'link', 'show', inside=True)
            unit.start(unit.launch_properties(files))
            files.listener.settimeout(5)
            channel, unused = files.listener.accept()
            with channel:
                channel.settimeout(8)
                pid, uid, gid = struct.unpack('3i', channel.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
                assert (pid, uid, gid) == (unit.identity.pid, 0, context.origin.identity.gid)
                ready = json.loads(channel.recv(65536))
                assert ready['event'] == 'ready' and ready['network'] == value['context']['network']
                unit.verify()
                assert ready['mount'] == list(node('/proc/'+str(pid)+'/ns/mnt'))
                assert ready['resolver'] == value['context']['resolver']['directory']
                assert ready['bus'] == value['context']['bus']['socket']
                assert ready['mount'] != list(node('/proc/self/ns/mnt'))
                assert unit.consumer_pids() == {pid}
                channel.send(json.dumps({'version': 1, 'operation': 'retire',
                                        'instance': instance, 'generation': 1}).encode())
                retired = json.loads(channel.recv(4096))
                assert retired == {'version': 1, 'event': 'retired', 'state': 'clean'}
            assert unit.process.exited(unit.pidfd, 5)
            unit.close()
            assert context.scope.network.network._ip('-j', 'link', 'show', inside=True) == baseline
            context.verify()
            files.verify()
            result = {'native_declaration_unit': True, 'private_worker_netns_and_mount': True,
                'private_resolver_bus_and_file': True, 'sealed_bootstrap_fd3': True,
                'owner_control_fd4': True, 'temporary_tun_created_and_retired': True,
                'original_links_preserved': True, 'owned_cgroup_retired': True,
                'production_broker_or_journal_implemented': False}
            intent.write_text(json.dumps({'instance':instance,'desktop':context.desktop_token,
                'unit':unit.name,'invocation':unit.identity.invocation,'phase':'accepted'})+'\n')
            completed = True
            return result
    finally:
        # Retain failed/unacknowledged native launches for inspection. Never
        # remove the prepared record or resume an owner over an unknown worker.
        if files is not None:
            files.close_handles()
        if completed:
            with context.scope.network.network.store.locked():
                files.verify()
                files.prepared.unlink()
                files.control.unlink()
                files.runtime.rmdir()
                files.state.rmdir()
                if not list(files.state.parent.iterdir()):
                    files.state.parent.rmdir()
        if completed or unit is None:
            assert entry.read_bytes() == entry_data and declaration_path.read_bytes() == declaration_data
            entry.unlink()
            declaration_path.unlink()
