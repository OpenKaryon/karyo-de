# SPDX-License-Identifier: GPL-3.0-or-later
"""A declaration-bound native unit, with a fixed DE entry before provider exec."""
from pathlib import Path
import re
import time

from .services import NativeProcess, OwnedUnit, ServiceError, program_identity, host_controller
from .worker_declaration import InstalledDeclaration

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
                expected = [b'/usr/bin/python3', b'-I', ENTRY.encode(), self.instance.encode(), b'']
                if arguments not in (expected, self.arguments):
                    raise ServiceError('The prepared worker changed its fixed command')
                # argv and exe are separate proc reads. The same-PID exec can
                # occur between them; no intermediate observation is published.
                time.sleep(.02)
                continue
            self.entered = True
            return result



class DeclaredUnit(OwnedUnit):
    def __init__(self, context, declaration, instance):
        if (not isinstance(declaration, InstalledDeclaration) or not isinstance(instance, str)
                or re.fullmatch('[a-f0-9]{32}', instance) is None):
            raise ValueError('A captured package declaration and worker instance are required')
        context.verify()
        declaration.verify()
        network = context.scope.network.network
        self.context, self.declaration, self.instance = context, declaration, instance
        self.store, self.spec, self.role, self.uid = network.store, network.spec, 'desktop-worker', 0
        self.name = 'karyo-desktop-worker-' + instance + '.service'
        self.program = declaration.declaration.arguments[0]
        self.command, self.network = network.command, network.identity
        self.process = EnteredProcess(instance, declaration.declaration.ready_seconds, declaration.declaration.arguments)
        self.host_guard = host_controller
        self.identity = self.executable = self.pidfd = None
        self.attempted = self.closed = False
        self.entry_identity = program_identity(ENTRY)

    def guard(self):
        super().guard()
        self.declaration.verify()
        if program_identity(ENTRY) != self.entry_identity:
            raise ServiceError('The installed worker entry changed')

    def execution_command(self, arguments):
        if arguments:
            raise ValueError('A desktop worker has no runtime execution arguments')
        return (ENTRY, self.instance)

    def stop_seconds(self):
        return self.declaration.declaration.cleanup_seconds

    def capture_executable(self):
        self.declaration.verify()
        return self.declaration.executable

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
