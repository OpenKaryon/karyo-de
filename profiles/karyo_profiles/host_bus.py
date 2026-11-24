# SPDX-License-Identifier: GPL-3.0-or-later
"""A profile-owned stock proxy for ordinary, non-network host services.

This primitive does not merge buses or select a default graphical bus. Network
clients use the separately captured private service bus. Physical networking
remains mediated by the host controller rather than exposed through this proxy.
"""
import os
from pathlib import Path
import stat
import time

from .services import OwnedUnit, ServiceError, ServiceRetained


HOST_SERVICES = (
    'org.freedesktop.login1', 'org.freedesktop.PolicyKit1',
    'org.freedesktop.UDisks2', 'org.freedesktop.UPower',
    'org.freedesktop.systemd1', 'org.freedesktop.Accounts',
    'org.freedesktop.hostname1', 'org.freedesktop.locale1',
    'org.freedesktop.timedate1', 'org.bluez',
)
HOST_ADDRESS = 'unix:path=/run/dbus/system_bus_socket'


def client_properties(files):
    socket = '/run/karyo-host-bus-' + files.network.spec.token + '/bus'
    values = tuple(value + ' ' + socket + ':/run/dbus/system_bus_socket'
                   if value.startswith('BindReadOnlyPaths=') else value
                   for value in files.plan.client_properties())
    return (*values, 'Environment=DBUS_SYSTEM_BUS_ADDRESS=' + HOST_ADDRESS)


class HostBusScope:
    def __init__(self, dns):
        self.dns, self.files = dns, dns.files
        self.network = self.files.network
        self.runtime = 'karyo-host-bus-' + self.network.spec.token
        self.directory = Path('/run') / self.runtime
        self.socket = self.directory / 'bus'
        self.unit = OwnedUnit(self.network.store, self.network.spec, 'host-bus', self.network.spec.uid,
                              self.network.command, network=self.network.identity)
        self.runtime_identity = self.socket_identity = None

    @property
    def address(self):
        return 'unix:path=' + str(self.socket)

    def properties(self):
        return ('User=' + str(self.network.spec.uid), 'Group=' + str(self.files.plan.gid),
                'NetworkNamespacePath=' + self.network.path,
                'RuntimeDirectory=' + self.runtime, 'RuntimeDirectoryMode=0700', 'UMask=0077',
                'NoNewPrivileges=yes', 'CapabilityBoundingSet=', 'AmbientCapabilities=',
                'PrivateTmp=yes', 'PrivateDevices=yes', 'ProtectSystem=strict',
                'RestrictAddressFamilies=AF_UNIX')

    def nodes(self):
        parent = os.open('/run', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
        try:
            directory = os.open(self.runtime, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                                dir_fd=parent)
        finally:os.close(parent)
        try:
            runtime = os.fstat(directory)
            node = os.stat('bus', dir_fd=directory, follow_symlinks=False)
            if (runtime.st_uid != self.network.spec.uid or runtime.st_gid != self.files.plan.gid
                    or stat.S_IMODE(runtime.st_mode) != 0o700 or not stat.S_ISSOCK(node.st_mode)
                    or node.st_uid != self.network.spec.uid or node.st_gid != self.files.plan.gid
                    or node.st_nlink != 1 or stat.S_IMODE(node.st_mode) & 0o077):
                raise ServiceError('The profile host-bus endpoint changed its owner or permissions')
            return (runtime.st_dev, runtime.st_ino), (node.st_dev, node.st_ino)
        finally:os.close(directory)

    def start(self):
        self.files.verify();self.dns.services.verify()
        if os.path.lexists(self.directory):raise ServiceError('An existing host-bus runtime directory is retained')
        self.unit.start(self.properties(), HOST_ADDRESS, str(self.socket), '--filter',
                        *('--talk=' + name for name in HOST_SERVICES))
        deadline = time.monotonic() + 5
        while True:
            try:self.runtime_identity, self.socket_identity = self.nodes();break
            except FileNotFoundError:
                if self.unit.process.exited(self.unit.pidfd) or time.monotonic() >= deadline:raise
                time.sleep(.02)
        self.verify()
        return self

    def verify(self):
        self.files.verify();self.dns.services.verify();self.unit.verify()
        if self.nodes() != (self.runtime_identity, self.socket_identity):raise ServiceRetained(self.unit)
        rows = {key:value.strip() for key, separator, value in
                (line.partition(':') for line in Path('/proc', str(self.unit.identity.pid), 'status').read_text().splitlines())
                if separator}
        if (rows.get('NoNewPrivs') != '1'
                or any(int(rows[key], 16) != 0 for key in ('CapEff', 'CapPrm', 'CapBnd', 'CapAmb'))):
            raise ServiceRetained(self.unit)
        self.unit.verify()

    def client_properties(self):
        self.verify()
        return client_properties(self.files)

    def descriptor(self):
        self.verify()
        parent = os.open(self.directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
        try:descriptor = os.open('bus',os.O_PATH | os.O_NOFOLLOW | os.O_CLOEXEC,dir_fd=parent)
        finally:os.close(parent)
        try:
            node = os.fstat(descriptor)
            if not stat.S_ISSOCK(node.st_mode) or (node.st_dev,node.st_ino) != self.socket_identity:
                raise ServiceRetained(self.unit)
            self.verify()
            return descriptor
        except BaseException:
            os.close(descriptor)
            raise

    def close(self):
        if not self.unit.closed:self.verify()
        self.unit.close()
        if os.path.lexists(self.directory):raise ServiceRetained(self.unit)

    def release_handles(self):
        if self.unit.pidfd is not None:self.unit.process.close(self.unit.pidfd);self.unit.pidfd = None
