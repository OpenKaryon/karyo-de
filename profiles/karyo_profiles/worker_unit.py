# SPDX-License-Identifier: GPL-3.0-or-later
"""A declaration-bound native unit, with a fixed DE entry before provider exec."""
from pathlib import Path
import re
import time

from .services import NativeProcess, OwnedUnit, ServiceError, program_identity, host_controller
from .worker_declaration import InstalledDeclaration
from .worker_origin import node
from .worker_errors import WorkerPending, WorkerRetained

ENTRY = '/usr/libexec/karyo/desktop-worker-entry'


class EnteredProcess(NativeProcess):
    """Wait only for the fixed entry's same-PID exec into its installed worker."""
    def __init__(self, instance, seconds, arguments):
        self.instance, self.seconds = instance, seconds
        self.entered = False
        self.arguments = [arg.encode() for arg in arguments] + [b'']

    def read(self, pid, executable, uid, cgroup, network):
        deadline = time.monotonic() + self.seconds
        while True:
            arguments = Path('/proc', str(pid), 'cmdline').read_bytes().split(b'\0')
            try:
                result = super().read(pid, executable, uid, cgroup, network)
                if arguments != self.arguments:
                    raise ServiceError('The worker has not entered its fixed package command')
            except ServiceError:
                if self.entered or time.monotonic() >= deadline:
                    raise
                # systemd may still be entering the fixed command, and argv
                # and exe are separate proc reads across same-PID exec. Wait
                # within the declaration bound; publish only the final exact
                # argv/executable/credentials/cgroup/namespace observation.
                time.sleep(.02)
                continue
            self.entered = True
            return result



class DeclaredUnit(OwnedUnit):
    def __init__(self, context, declaration, instance, *, recovery=False):
        if (not isinstance(declaration, InstalledDeclaration) or not isinstance(instance, str)
                or re.fullmatch('[a-f0-9]{32}', instance) is None):
            raise ValueError('A captured package declaration and worker instance are required')
        context.verify()
        declaration.verify()
        network = context.scope.network.network
        self.context, self.declaration, self.instance = context, declaration, instance
        self.store, self.spec, self.role, self.uid = network.store, network.spec, 'desktop-worker', 0
        self.name = 'karyo-desktop-worker-' + instance + '.service'
        self.recovery = recovery
        self.arguments = (declaration.declaration.recovery_arguments if recovery else declaration.declaration.arguments)
        self.program = self.arguments[0]
        self.command, self.network = network.command, network.identity
        self.process = EnteredProcess(instance, declaration.declaration.ready_seconds, self.arguments)
        self.host_guard = host_controller
        self.identity = self.executable = self.pidfd = None
        self.attempted = self.closed = False
        self.entry_identity = program_identity(ENTRY)

    def guard(self):
        super().guard()
        if not self.attempted:
            self.declaration.verify()
            if program_identity(ENTRY) != self.entry_identity:
                raise ServiceError('The installed worker entry changed')

    def execution_command(self, arguments):
        if arguments:
            raise ValueError('A desktop worker has no runtime execution arguments')
        self.declaration.verify()
        if program_identity(ENTRY) != self.entry_identity:
            raise ServiceError('The installed worker entry changed before launch')
        return (ENTRY, self.instance)

    def stop_seconds(self):
        return self.declaration.declaration.cleanup_seconds

    def capture_executable(self):
        self.declaration.verify()
        return self.declaration.recovery_executable if self.recovery else self.declaration.executable

    def launch_properties(self, files):
        # The owner supplies captured objects, never GUI-selected paths.
        from .worker_files import WorkerFiles
        if not isinstance(files, WorkerFiles) or files.instance != self.instance:
            raise ValueError('The worker needs its captured instance files')
        files.verify()
        if files.prepared_digest is None:
            raise ValueError('The worker bootstrap has not been published')
        runtime, state = files.runtime, files.state
        plan = self.context.scope.dns.files.plan
        caps = ' '.join(self.declaration.declaration.capabilities)
        return ('User=root', 'Group=' + str(plan.gid), 'UMask=0077', 'NoNewPrivileges=yes',
            'NetworkNamespacePath=' + self.context.scope.network.network.path,
            'Environment=DBUS_SYSTEM_BUS_ADDRESS=' + plan.bus_address,
            'CapabilityBoundingSet=' + caps, 'AmbientCapabilities=' + caps,
            'PrivateTmp=yes', 'PrivateDevices=yes', 'ProtectSystem=strict',
            'ProtectHome=yes', 'TemporaryFileSystem=/run:ro', 'ReadOnlyPaths=/sys',
            'BindPaths=/dev/net/tun:/dev/net/tun ' + str(runtime) + ':' + str(runtime),
            'BindReadOnlyPaths=' + plan.runtime_paths[0] + ':/run/systemd/resolve '
                + plan.runtime_paths[1] + ':' + plan.runtime_paths[1] + ' '
                + plan.bus_address.removeprefix('unix:path=') + ':/run/dbus/system_bus_socket '
                + str(files.prepared) + ' ' + str(files.control),
            'ReadWritePaths=' + str(runtime) + ' ' + str(state),
            'RestrictAddressFamilies=AF_UNIX AF_NETLINK AF_INET AF_INET6',
            'DevicePolicy=closed', 'DeviceAllow=/dev/net/tun rw',
            'MemoryMax=128M', 'TasksMax=32')

    def receipt(self, files):
        self.verify()
        proof = self.identity
        return {'native': {'invocation':proof.invocation,'pid':proof.pid,'start':proof.start,
                    'exe':list(self.executable),'namespace':list(self.network),
                    'runtime':[list(files.identities[files.runtime][:2])]},
                'mount':list(node('/proc/'+str(proof.pid)+'/ns/mnt')),
                'cgroup':list(node('/sys/fs/cgroup'+proof.cgroup)), 'entry':list(self.entry_identity)}

    @classmethod
    def restore(cls, scope, declaration, instance, value, *, recovery=False):
        from types import SimpleNamespace
        from .services import UnitIdentity
        obj = cls.__new__(cls)
        network = scope.network.network
        obj.context = SimpleNamespace(scope=scope)
        obj.declaration, obj.instance, obj.recovery = declaration, instance, recovery
        obj.arguments = declaration.declaration.recovery_arguments if recovery else declaration.declaration.arguments
        obj.store, obj.spec, obj.role, obj.uid = network.store, network.spec, 'desktop-worker', 0
        obj.name = 'karyo-desktop-worker-'+instance+'.service'
        obj.program, obj.command, obj.network = obj.arguments[0], network.command, network.identity
        obj.process = EnteredProcess(instance,declaration.declaration.ready_seconds,obj.arguments)
        obj.process.entered = True
        obj.host_guard = host_controller
        obj.entry_identity = tuple(value['entry'])
        proof = value['native']
        obj.executable = tuple(proof['exe'])
        obj.identity = UnitIdentity(proof['invocation'],proof['pid'],'/system.slice/'+obj.name,proof['start'])
        obj.attempted, obj.closed, obj.pidfd = True, False, None
        from .journal_dns import process_gone
        try:
            if not process_gone(proof['pid'],proof['start']):
                try:obj.pidfd = obj.process.open(proof['pid'])
                except ProcessLookupError:pass
                else:
                    if obj.process.exited(obj.pidfd):
                        obj.process.close(obj.pidfd)
                        obj.pidfd = None
            rows = obj.info()
            if obj.pidfd is not None:
                obj.verify()
                if (node('/proc/'+str(proof['pid'])+'/ns/mnt') != tuple(value['mount'])
                        or node('/sys/fs/cgroup'+obj.identity.cgroup) != tuple(value['cgroup'])):
                    raise WorkerRetained('The captured worker mount/cgroup lifetime changed')
            else:
                if rows['LoadState'] != 'not-found':
                    if (rows['Transient'] != 'yes' or rows['InvocationID'] != proof['invocation']
                            or rows['MainPID'] not in {'0',str(proof['pid'])}
                            or rows['ControlGroup'] not in {'',obj.identity.cgroup}):
                        raise WorkerRetained('The dead worker service identity changed')
                    if (rows['ActiveState'] not in {'inactive','failed'} or rows['ControlGroup']
                            or rows['MainPID'] != '0'):
                        raise WorkerPending('The same worker is still completing its native stop')
                if obj.cgroup_pids():
                    raise WorkerRetained('The completed worker still has cgroup consumers')
                obj.closed = True
            return obj
        except BaseException:
            if obj.pidfd is not None:
                obj.process.close(obj.pidfd)
                obj.pidfd = None
            raise
