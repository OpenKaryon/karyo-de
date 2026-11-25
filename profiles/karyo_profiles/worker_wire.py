# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact external worker records; 64-bit kernel values use decimal strings."""
from dataclasses import asdict
import fcntl
import json
import os
from pathlib import Path

from .services import host_controller
from .physical_caller import start_time as process_start
from .worker_origin import node


def integer(value):
    if (not isinstance(value,str) or not value.isascii() or not value.isdecimal()
            or not value or value[0] == '0' or not 0 < int(value) < 2**64):
        raise ValueError('A canonical positive decimal kernel value is required')
    return int(value)


def pair(value):
    if not isinstance(value,list) or len(value) != 2:
        raise ValueError('A kernel identity needs two decimal values')
    return tuple(integer(item) for item in value)


def wire_pair(value):
    if len(value) != 2 or any(type(item) is not int or not 0 < item < 2**64 for item in value):
        raise ValueError('Invalid captured kernel identity')
    return [str(item) for item in value]


def origin(value):
    value = dict(value)
    value['start'] = str(value['start'])
    for key in ('executable','network','mount','user','cgroup_node'):
        value[key] = wire_pair(value[key])
    return value


def owner_identity(bus, epoch):
    host_controller()
    daemon = bus.get_object('org.freedesktop.DBus','/org/freedesktop/DBus',introspect=False)
    call = lambda method: daemon.get_dbus_method(method,'org.freedesktop.DBus')
    name = str(bus.get_unique_name())
    if int(call('GetConnectionUnixUser')(name)) != 0 or int(call('GetConnectionUnixProcessID')(name)) != os.getpid():
        raise PermissionError('The worker owner requires its direct root bus connection')
    return {'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
            'epoch':epoch, 'bus_id':str(call('GetId')()), 'bus_owner':name,
            'pid':os.getpid(), 'start':str(process_start(os.getpid()))}


def prepared(context, declaration, files, owner, *, mode='serve', original_origin=None):
    host_controller()
    files.verify()
    declaration.verify()
    scope, instance = context.scope, files.instance
    scope.network.network.verify()
    scope.dns.services.verify()
    if mode not in {'serve','recover'} or mode == 'serve' and files.recovery_from is not None:
        raise ValueError('Invalid worker launch role')
    plan = scope.dns.files.plan
    gui = asdict(context.origin.identity) if original_origin is None else original_origin
    manager = scope.manager.identity
    if manager is None and mode == 'serve':
        raise PermissionError('A serving worker needs a captured live desktop manager')
    record = {'version':1,'role':'managed-worker-bootstrap','owner':dict(owner),
        'desktop':{'id':scope.token,'generation':'1','manager':None if manager is None else {
            'invocation':manager.invocation,'pid':manager.pid,'start':str(manager.start)}},
        'worker':{'id':instance,'generation':'1','mode':mode,
                  'unit':'karyo-desktop-worker-'+instance+'.service',
                  'recovery_from':files.recovery_from},
        'declaration':{'id':declaration.declaration.identifier,
                       'revision':declaration.declaration.revision,'sha256':declaration.digest},
        'context':{'network':wire_pair(scope.network.network.identity), 'user':wire_pair(gui['user']),
            'gui_mount':wire_pair(gui['mount']), 'service_gid':plan.gid,
            'resolver':{'directory':wire_pair(node(plan.runtime_paths[0])),
                        'invocation':scope.dns.services.resolver.identity.invocation,
                        'pid':scope.dns.services.resolver.identity.pid,
                        'start':str(scope.dns.services.resolver.identity.start)},
            'bus':{'address':plan.bus_address,
                   'socket':wire_pair(node(plan.bus_address.removeprefix('unix:path='))),
                   'invocation':scope.dns.services.bus.identity.invocation,
                   'pid':scope.dns.services.bus.identity.pid,'start':str(scope.dns.services.bus.identity.start)}},
        'gui':origin(gui),
        'directories':{name:{'path':str(path),'node':wire_pair(node(path))}
                       for name,path in (('runtime',files.runtime),('state',files.state))},
        'cleanup':{'ready_seconds':declaration.declaration.ready_seconds,
                   'cleanup_seconds':declaration.declaration.cleanup_seconds,
                   'stop_application':declaration.declaration.stop_application}}
    record['directories']['endpoint'] = str(files.runtime/'ipc')
    return record


def activation(prepared_record, unit, files, request, endpoint):
    unit.verify()
    files.verify()
    value = json.loads(json.dumps(prepared_record))
    value['role'] = 'managed-worker-activation'
    value['request'] = request
    value['prepared_sha256'] = files.prepared_digest
    current = unit.identity
    value['worker']['process'] = {'pid':current.pid,'start':str(current.start),
        'uid':0,'gid':unit.context.scope.dns.files.plan.gid,
        'executable':wire_pair(unit.executable),'network':wire_pair(unit.network),
        'mount':wire_pair(node('/proc/'+str(current.pid)+'/ns/mnt')),
        'cgroup':current.cgroup,'cgroup_node':wire_pair(node('/sys/fs/cgroup'+current.cgroup)),
        'invocation':current.invocation}
    value['endpoint'] = {'path':str(files.runtime/'ipc'),'node':wire_pair(endpoint),
        'uid':0,'gid':unit.context.scope.dns.files.plan.gid,'mode':0o660}
    return value


def sealed_record(value):
    data = json.dumps(value,ensure_ascii=True,allow_nan=False,separators=(',',':'),sort_keys=True).encode('ascii')
    return sealed_bytes(data)


def sealed_bytes(data):
    if not isinstance(data,bytes) or not 1 <= len(data) <= 65536:
        raise ValueError('Worker activation exceeds its record bound')
    descriptor = os.memfd_create('karyo-worker-activation',os.MFD_CLOEXEC|os.MFD_ALLOW_SEALING)
    try:
        view = memoryview(data)
        while view:
            written = os.write(descriptor,view)
            if written <= 0:
                raise OSError('Incomplete worker activation record')
            view = view[written:]
        os.lseek(descriptor,0,os.SEEK_SET)
        fcntl.fcntl(descriptor,fcntl.F_ADD_SEALS,
            fcntl.F_SEAL_SEAL|fcntl.F_SEAL_WRITE|fcntl.F_SEAL_GROW|fcntl.F_SEAL_SHRINK)
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise
