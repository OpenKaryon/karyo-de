# SPDX-License-Identifier: GPL-3.0-or-later
"""Write-ahead desktop worker collection, independent of native VPN daemons."""
from dataclasses import asdict
import os
import json
from pathlib import Path
import select
import time
from types import SimpleNamespace
import uuid

from .journal import JournalRetained
from .worker_control import WorkerControl
from .worker_declaration import InstalledDeclaration
from .worker_errors import WorkerPending, WorkerRetained
from .worker_files import WorkerFiles
from .worker_origin import node
from .worker_record import REMOVING
from .worker_unit import DeclaredUnit
from .worker_wire import activation, prepared, sealed_record


class RecoveryContext:
    def __init__(self,scope):
        self.scope,self.desktop_token = scope,scope.token

    def verify(self):
        scope = self.scope
        if not scope.network.network.store._locked:
            raise PermissionError('Worker recovery requires its profile owner lock')
        scope.network.network.verify()
        scope.dns.services.verify()


class WorkerCollection:
    def __init__(self,scope):
        self.scope,self.journal = scope,scope.journal
        self.live = {}
        self.notify = self.owner = None

    def record(self,instance):
        return self.journal.record(self.scope.token)['workers'][instance]

    def change(self,instance,**fields):
        before = self.journal.record(self.scope.token)
        workers = dict(before.get('workers',{}))
        workers[instance] = dict(workers[instance],**fields)
        self.journal.change(before,dict(before,workers=workers))

    def add(self,instance,value):
        before = self.journal.record(self.scope.token)
        workers = dict(before.get('workers',{}))
        if instance in workers or len(workers) >= 16:
            raise WorkerRetained('The desktop worker collection is occupied')
        workers[instance] = value
        self.journal.change(before,dict(before,workers=workers))

    def forget(self,instance):
        before = self.journal.record(self.scope.token)
        workers = dict(before['workers'])
        del workers[instance]
        after = dict(before)
        if workers:after['workers'] = workers
        else:after.pop('workers')
        self.journal.change(before,after)
        item = self.live.pop(instance)
        self.handles(item)

    def restore(self):
        for instance,value in self.journal.record(self.scope.token).get('workers',{}).items():
            installed = InstalledDeclaration.from_receipt(value['declaration'])
            if value['files'] is None or value['unit'] is None and value['phase'] not in REMOVING:
                raise WorkerRetained('An unacknowledged worker preparation needs review')
            files = WorkerFiles.restore(self.scope,instance,value['files'],partial=value['phase'] in REMOVING)
            unit = None if value['unit'] is None else DeclaredUnit.restore(self.scope,installed,instance,value['unit'])
            self.live[instance] = SimpleNamespace(files=files,unit=unit,declaration=installed,
                control=None,context=None,prepared=None,recovery=None,deadline=None,finishing=False,cleanup_ack=False,stop_requested=False)
            recovery = value['recovery']
            if recovery is not None:
                if recovery['files'] is None or recovery['unit'] is None:
                    raise WorkerRetained('An unacknowledged worker recovery needs review')
                rf = WorkerFiles.restore(self.scope,recovery['id'],recovery['files'],
                    state_instance=instance,partial=recovery['phase'] in REMOVING)
                ru = DeclaredUnit.restore(self.scope,installed,recovery['id'],recovery['unit'],recovery=True)
                self.live[instance].recovery = SimpleNamespace(files=rf,unit=ru,control=None)
            if value['phase'] not in REMOVING:
                # The former owner channel and activation cannot survive its
                # bus/epoch loss. Retire known objects; never republish them.
                self.retire(instance)
        return self

    def start(self,context,installed,owner,request,*,before_commit,progress=None):
        progress = (lambda unused:None) if progress is None else progress
        progress('context')
        context.verify()
        self.owner = owner
        for instance,item in self.live.items():
            value = self.record(instance)
            if (value['phase'] is None and item.context is not None and item.control is not None
                    and item.control.committed and not item.control.retiring
                    and item.declaration.digest == installed.digest
                    and item.declaration.executable == installed.executable
                    and value['source']['epoch'] == owner['epoch']
                    and value['source']['owner'] == owner['bus_owner']
                    and value['source']['bus'] == owner['bus_id']
                    and value['source']['sender'] == context.origin.sender
                    and value['origin'] == json.loads(json.dumps(asdict(context.origin.identity)))):
                progress('reacquire-current')
                before_commit()
                context.verify()
                installed.verify()
                progress('reacquire-unit')
                item.control.verify_view()
                if node(item.files.runtime/'ipc') != tuple(value['endpoint']):
                    raise WorkerRetained('The existing worker endpoint changed')
                progress('reacquire-record')
                descriptor = sealed_record(activation(item.prepared,item.unit,item.files,request,tuple(value['endpoint'])))
                try:self.change(instance,request=request)
                except BaseException:
                    os.close(descriptor)
                    raise
                previous = item.context.origin
                item.context = context
                if previous is not context.origin:previous.close()
                progress('reacquire-complete')
                return instance,descriptor
        if installed.declaration.resources.interfaces:
            if (any(item.declaration.declaration.resources.interfaces for item in self.live.values())
                    or set(self.scope.network.network._inventory(inside=True))-{'lo','eth0'}):
                raise WorkerPending('A VPN already uses this desktop network')
        for item in self.live.values():
            if installed.declaration.resources.conflicts(item.declaration.declaration.resources):
                raise WorkerPending('An existing worker owns overlapping desktop resources')
        installed.declaration.resources.absent(self.scope.network.network)
        instance = uuid.uuid4().hex
        gui = json.loads(json.dumps(asdict(context.origin.identity)))
        self.add(instance,{'version':1,'request':request,'source':{
            'bus':owner['bus_id'],'owner':owner['bus_owner'],'epoch':owner['epoch'],
            'sender':context.origin.sender,'pid':owner['pid'],'start':int(owner['start'])},
            'origin':gui,'declaration':installed.receipt(),'phase':'files-create',
            'files':None,'unit':None,'endpoint':None,'recovery':None})
        files = WorkerFiles(context,instance)
        item = SimpleNamespace(files=files,unit=None,declaration=installed,control=None,
            context=context,prepared=None,recovery=None,deadline=None,finishing=False,cleanup_ack=False,stop_requested=False)
        self.live[instance] = item
        try:
            files.prepare()
            item.prepared = prepared(context,installed,files,owner)
            files.publish_prepared(item.prepared)
            self.change(instance,phase='unit-start',files=files.receipt())
            item.unit = DeclaredUnit(context,installed,instance)
            progress('native-start')
            item.unit.start(item.unit.launch_properties(files))
            self.change(instance,phase='ready-wait',unit=item.unit.receipt(files))
            item.control = WorkerControl.accept(item.unit,files)
            progress('wait-ready')
            endpoint = item.control.ready()
            before_commit()
            context.verify()
            installed.verify()
            progress('commit')
            item.control.commit()
            before_commit()
            context.verify()
            progress('final-record')
            result = sealed_record(activation(item.prepared,item.unit,files,request,endpoint))
            try:self.change(instance,phase=None,endpoint=list(endpoint))
            except BaseException:
                os.close(result)
                raise
            return instance,result
        except BaseException:
            current = self.record(instance)
            if current['files'] is not None and current['unit'] is not None:
                self.retire(instance)
            else:
                # Unknown partial preparation is retained; no fabricated
                # native acknowledgement authorizes stop/removal.
                self.change(instance,phase='retained') if current['files'] is not None else None
            raise

    def consumer_roots(self):
        roots = []
        for instance,item in self.live.items():
            for active in (item,item.recovery):
                if active is None or active.unit is None or active.unit.closed:continue
                unit = active.unit
                if unit.pidfd is not None and unit.process.exited(unit.pidfd):
                    proof = self.record(instance)['unit' if active is item else 'recovery']
                    if active is not item:proof = proof['unit']
                    replacement = DeclaredUnit.restore(self.scope,item.declaration,unit.instance,
                                                       proof,recovery=active is not item)
                    if replacement.closed:
                        unit.process.close(unit.pidfd)
                        unit.pidfd = None
                        unit.closed = True
                        continue
                    replacement.process.close(replacement.pidfd)
                unit.verify()
                roots.append(unit.identity.cgroup)
        return tuple(roots)

    def retire(self,instance):
        item = self.live[instance]
        value = self.record(instance)
        if value['phase'] in REMOVING:return
        if value['phase'] == 'retained':
            raise WorkerRetained('This worker needs administrator recovery')
        if value['files'] is None or value['unit'] is None:
            raise WorkerRetained('This worker has no complete native launch receipt')
        if item.deadline is None:
            item.deadline = time.monotonic()+item.declaration.declaration.cleanup_seconds
            self.change(instance,phase='retire')
            if self.notify is not None:self.notify(instance,value,item.declaration.declaration.cleanup_seconds*1000)
            if item.control is not None:
                try:item.control.retire()
                except OSError:item.control.close();item.control=None

    def kill_application(self,item,value):
        # A surviving GUI may retain a TUN descriptor. Only its originally
        # captured, still-live app scope may be stopped; never all UID tasks.
        if item.context is None or not item.declaration.declaration.stop_application:
            raise WorkerRetained('The registered application has not released its resources')
        item.context.verify(require_active=False)
        origin = value['origin']
        directory = os.open('/sys/fs/cgroup'+origin['cgroup'],os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW|os.O_CLOEXEC)
        try:
            info = os.fstat(directory)
            if (info.st_dev,info.st_ino) != tuple(origin['cgroup_node']):
                raise WorkerRetained('The captured application scope changed before retirement')
            descriptor = os.open('cgroup.kill',os.O_WRONLY|os.O_NOFOLLOW|os.O_CLOEXEC,dir_fd=directory)
            try:os.write(descriptor,b'1\n')
            finally:os.close(descriptor)
        finally:os.close(directory)

    def collect_files(self,instance):
        item = self.live[instance]
        value = self.record(instance)
        order = ('runtime','state','prepared','control')
        first = order.index(value['phase'].removeprefix('remove-')) if value['phase'] in REMOVING else 0
        for position,kind in enumerate(order[first:],first):
            replay = value['phase'] == 'remove-'+kind
            self.change(instance,phase='remove-'+kind)
            item.files.retire_item(kind,missing_ok=replay)
        self.forget(instance)

    def poll_one(self,instance,owner):
        item,value = self.live[instance],self.record(instance)
        if value['phase'] in REMOVING:
            self.collect_files(instance)
            return
        if value['phase'] == 'retained':
            raise WorkerRetained('Worker cleanup remains retained')
        if value['phase'] is None:
            try:item.context.verify(require_active=False)
            except (OSError,RuntimeError,PermissionError):self.retire(instance)
            if item.unit.pidfd is not None and item.unit.process.exited(item.unit.pidfd):self.retire(instance)
            return
        self.retire(instance)
        active = item.recovery or item
        unit = active.unit
        if unit is None:raise WorkerRetained('The retiring worker lost its native receipt')
        if active.control is not None and not item.cleanup_ack and not item.finishing:
            if select.select([active.control.channel],[],[],0)[0]:
                try:state = active.control.retired()
                except (OSError,ValueError):active.control.close();active.control=None
                else:
                    if state != 'clean':
                        active.control.close();active.control=None
                    else:
                        item.cleanup_ack = True
        remaining = item.declaration.declaration.resources.present(self.scope.network.network)
        if item.cleanup_ack and not item.finishing and not any(remaining.values()):
            active.control.finish()
            item.finishing = True
        if unit.pidfd is not None and not unit.process.exited(unit.pidfd):
            if time.monotonic() >= item.deadline and not item.stop_requested:
                if remaining['interfaces']:
                    self.kill_application(item,value)
                unit.verify()
                unit.command.run('/usr/bin/systemctl','--no-block','--job-mode=fail','stop',unit.name)
                self.change(instance,phase='unit-stop')
                item.stop_requested = True
                raise WorkerPending('Waiting for the captured worker stop after its deadline')
            return
        self.consumer_roots()
        if not unit.closed:raise WorkerPending('Waiting for the captured worker cgroup to retire')
        if any(item.declaration.declaration.resources.present(self.scope.network.network).values()):
            if active is not item:
                raise WorkerRetained('The recovery did not release its declared resources')
            self.start_recovery(instance,owner)
            return
        if item.recovery is not None:
            self.collect_recovery(instance)
        item.files.close_handles()
        self.collect_files(instance)

    def start_recovery(self,instance,owner):
        if owner is None:raise WorkerRetained('Worker recovery has no current authenticated producer')
        item = self.live[instance]
        context = RecoveryContext(self.scope)
        installed = item.declaration
        installed.verify()
        fresh = uuid.uuid4().hex
        recovery = {'id':fresh,'phase':'files-create','files':None,'unit':None}
        self.change(instance,phase='recovery-start',recovery=recovery)
        files = WorkerFiles.for_recovery(context,item.files,fresh).prepare()
        record = prepared(context,installed,files,owner,mode='recover',original_origin=self.record(instance)['origin'])
        files.publish_prepared(record)
        recovery = dict(recovery,phase='unit-start',files=files.receipt())
        self.change(instance,recovery=recovery)
        unit = DeclaredUnit(context,installed,fresh,recovery=True)
        item.recovery = SimpleNamespace(files=files,unit=unit,control=None)
        unit.start(unit.launch_properties(files))
        recovery = dict(recovery,phase='ready-wait',unit=unit.receipt(files))
        self.change(instance,phase='recovery-wait',recovery=recovery)
        control = item.recovery.control = WorkerControl.accept(unit,files)
        control.ready()
        control.commit()
        control.retire()
        item.finishing = item.cleanup_ack = item.stop_requested = False
        item.deadline = time.monotonic()+installed.declaration.cleanup_seconds

    def collect_recovery(self,instance):
        item,value = self.live[instance],self.record(instance)
        recovery = value['recovery']
        active = item.recovery
        if not active.unit.closed:raise WorkerPending('The recovery cgroup is still active')
        active.files.close_handles()
        order = ('runtime','prepared','control')
        first = order.index(recovery['phase'].removeprefix('remove-')) if recovery['phase'] in REMOVING else 0
        for kind in order[first:]:
            replay = recovery['phase'] == 'remove-'+kind
            recovery = dict(recovery,phase='remove-'+kind)
            self.change(instance,recovery=recovery)
            active.files.retire_item(kind,missing_ok=replay)
        self.change(instance,recovery=None)
        self.handles(active)
        item.recovery = None

    def poll(self,owner=None):
        owner = self.owner if owner is None else owner
        for instance in tuple(self.live):
            try:self.poll_one(instance,owner)
            except WorkerPending:continue

    def close(self,owner=None):
        owner = self.owner if owner is None else owner
        for instance in tuple(self.live):self.retire(instance)
        self.poll(owner)
        if self.live:raise WorkerPending('Waiting for registered desktop workers to retire')

    @staticmethod
    def handles(item):
        if item.control is not None:item.control.close();item.control=None
        item.files.close_handles()
        if item.unit is not None and item.unit.pidfd is not None:
            item.unit.process.close(item.unit.pidfd);item.unit.pidfd=None
        if getattr(item,'recovery',None) is not None:WorkerCollection.handles(item.recovery)
        if getattr(item,'context',None) is not None:item.context.origin.close()

    def release_handles(self):
        for item in self.live.values():self.handles(item)
