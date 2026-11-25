# SPDX-License-Identifier: GPL-3.0-or-later
"""Own native QA placement probe; not a production activation implementation."""
import json
from dataclasses import asdict
from types import SimpleNamespace
import dbus
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
from karyo_profiles.worker_record import validate_workers
from karyo_profiles.worker_wire import prepared, owner_identity, wire_pair, activation, sealed_record
from karyo_profiles.worker_control import WorkerControl, send, receive


def probe(context, request):
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
        'argv': ['/usr/bin/python3', '-I', str(folder/'native_worker_fixture.py'), '--control-exercise'],
        'recover_argv': ['/usr/bin/python3', '-I', str(folder/'native_worker_fixture.py'), '--recover'],
        'resources': {'interfaces':['karyo-fixture0'],
            'route_tables':[{'family':'ipv4','table':42000}],
            'rule_tables':[{'family':'ipv4','table':42000}],
            'route_protocols':[{'family':'ipv4','table':254,'protocol':200},{'family':'ipv6','table':254,'protocol':200}],
            'nft_tables':[['inet','karyo_fixture']]},
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
    previous_workers = getattr(context.scope,'workers',None)
    files = unit = None
    completed = False
    try:
        with context.scope.network.network.store.locked():
            context.verify()
            installed = InstalledDeclaration.capture('org.karyo.Fixture')
            installed.declaration.resources.absent(context.scope.network.network)
            files = WorkerFiles(context, instance).prepare()
            plan = context.scope.dns.files.plan
            value = prepared(context, installed, files, owner_identity(context.origin.bus, uuid.uuid4().hex))
            files.publish_prepared(value)
            unit = DeclaredUnit(context, installed, instance)
            baseline = context.scope.network.network._ip('-j', 'link', 'show', inside=True)
            unit.start(unit.launch_properties(files))
            def consumer_roots():
                if unit.closed:return ()
                unit.verify()
                return (unit.identity.cgroup,)
            assert previous_workers is None or not previous_workers.live
            context.scope.workers = SimpleNamespace(consumer_roots=consumer_roots)
            control = WorkerControl.accept(unit,files)
            channel = control.channel
            pid = unit.identity.pid
            with channel:
                endpoint = control.ready()
                installed.declaration.resources.absent(context.scope.network.network)
                context.verify()
                control.commit()
                result_fd = sealed_record(activation(value,unit,files,request,endpoint))
                try:
                    final = json.loads(os.read(result_fd,65537))
                    assert final['worker']['process']['start'] == str(unit.identity.start)
                    assert final['endpoint']['node'] == wire_pair(endpoint)
                    assert final['gui']['start'] == str(context.origin.identity.start)
                    assert final['owner']['bus_owner'] == context.origin.bus.get_unique_name()
                finally:
                    os.close(result_fd)
                unit.verify()
                observed = installed.declaration.resources.present(context.scope.network.network)
                assert observed['interfaces'] == ['karyo-fixture0']
                assert all(observed.values())
                assert len(observed['route_protocols']) == 2
                assert unit.consumer_pids() == {pid}
                native_receipt = unit.receipt(files)
                daemon = dbus.Interface(context.origin.bus.get_object('org.freedesktop.DBus',
                    '/org/freedesktop/DBus'),'org.freedesktop.DBus')
                stored = {'version':1,'request':request,'source':{'bus':str(daemon.GetId()),
                    'sender':context.origin.sender,'owner':str(context.origin.bus.get_unique_name()),
                    'epoch':value['owner']['epoch'],'pid':os.getpid(),'start':process_start(os.getpid())},
                    'origin':asdict(context.origin.identity),'declaration':installed.receipt(),
                    'phase':'ready-wait','files':files.receipt(),'unit':native_receipt,
                    'endpoint':None,'recovery':None}
                stored = json.loads(json.dumps(stored))
                validate_workers({instance:stored},context.origin.identity.uid,plan.gid,
                                 list(context.scope.network.network.identity))
                observer = DeclaredUnit.restore(context.scope,installed,instance,native_receipt)
                observer.verify()
                observer.process.close(observer.pidfd)
                observer.pidfd = None

                control.retire()
                assert receive(channel)[0] == control.packet('event','link_closed')
                partial = installed.declaration.resources.present(context.scope.network.network)
                assert partial['interfaces'] == [] and len(partial['route_protocols']) == 2
                assert partial['route_tables'] and partial['rule_tables'] and partial['nft_tables']
                send(channel,control.packet('operation','finish_cleanup'))
                assert control.retired() == 'clean'
                installed.declaration.resources.absent(context.scope.network.network)
                control.finish()
            assert unit.process.exited(unit.pidfd, 5)
            unit.close()
            completed_observer = DeclaredUnit.restore(context.scope,installed,instance,native_receipt)
            assert completed_observer.closed
            assert context.scope.network.network._ip('-j', 'link', 'show', inside=True) == baseline
            context.verify()
            installed.declaration.resources.absent(context.scope.network.network)
            files.verify()
            result = {'native_declaration_unit': True, 'private_worker_netns_and_mount': True,
                'private_resolver_bus_and_file': True, 'sealed_bootstrap_fd3': True,
                'owner_control_fd4': True, 'temporary_tun_created_and_retired': True,
                'original_links_preserved': True, 'declared_resource_cleanup_checked': True,
                'ipv4_ipv6_routes_rules_and_nft_observed_after_tun_close': True, 'owned_cgroup_retired': True,
                'production_broker_or_journal_implemented': False, 'typed_record_roundtrip': True,
                'worker_listener_fd_and_commit_verified':True,'exact_sealed_activation_record':True,
                'live_and_retired_unit_observers_reattached':True}
            intent.write_text(json.dumps({'instance':instance,'desktop':context.desktop_token,
                'unit':unit.name,'invocation':unit.identity.invocation,'phase':'accepted'})+'\n')
            completed = True
            return result
    finally:
        context.scope.workers = previous_workers
        # Retain failed/unacknowledged native launches for inspection. Never
        # remove the prepared record or resume an owner over an unknown worker.
        if files is not None:
            files.close_handles()
        if completed:
            with context.scope.network.network.store.locked():
                files.verify()
                receipt = files.receipt()
                intent.write_text(json.dumps({'instance':instance,'desktop':context.desktop_token,
                    'unit':unit.name,'invocation':unit.identity.invocation,'phase':'remove-runtime',
                    'files':receipt})+'\n')
                files.retire_item('runtime')
                replay = WorkerFiles.restore(context.scope, instance, receipt, partial=True)
                assert not replay.retire_item('runtime', missing_ok=True)
                for kind in ('state','prepared','control'):
                    intent.write_text(json.dumps({'instance':instance,'desktop':context.desktop_token,
                        'phase':'remove-'+kind,'files':receipt})+'\n')
                    replay.retire_item(kind)
                if not list(files.state.parent.iterdir()):
                    files.state.parent.rmdir()
                result['file_removal_replay'] = True
                from native_worker_collection import probe as collection_probe
                result.update(collection_probe(context,declaration_path))
        if completed or unit is None:
            assert entry.read_bytes() == entry_data and declaration_path.read_bytes() == declaration_data
            entry.unlink()
            declaration_path.unlink()
