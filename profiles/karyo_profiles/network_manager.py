# SPDX-License-Identifier: GPL-3.0-or-later
"""Run an unchanged NetworkManager against one captured desktop network/bus.

Physical hardware belongs to the host's forwarding owner. NetworkManager uses
the DE-generated temporary base connection on its captured namespace uplink and
owns connections explicitly created in this private instance. Its persistent
and runtime paths are mounts of this instance's own state, not host directories.
"""
import os
from pathlib import Path
import stat
import time

from .provider_state import ProviderState, STATE_ROOT
from .services import OwnedUnit, ServiceError, ServiceRetained, program_identity
from .storage import atomic_write, trusted_directory

CONFIGURATION = ("# Karyo private NetworkManager instance.\n"
                 "[main]\nplugins=keyfile\nno-auto-default=*\nauth-polkit=false\n"
                 "dns=systemd-resolved\nrc-manager=unmanaged\nhostname-mode=none\n"
                 "[keyfile]\nunmanaged-devices=interface-name:lo\n"
                 "[device-karyo-uplink]\nmatch-device=interface-name:eth0\nmanaged=1\n"
                 "[connectivity]\nenabled=false\n[logging]\nlevel=INFO\n").encode()


class NetworkManagerScope:
    def __init__(self, dns, *, state_root=STATE_ROOT, clients=False):
        self.dns, self.files = dns, dns.files
        self.network = self.files.network
        if type(clients) is not bool:raise ValueError('The network-client publication must be explicit')
        self.clients = clients
        if self.files.plan.service_view != 'network-manager':
            raise ServiceError('NetworkManager requires the captured private network-service bus')
        self.state = ProviderState(self.files, 'network-manager', state_root)
        self.runtime = 'karyo-network-manager-' + self.network.spec.token
        self.directory = Path('/run') / self.runtime
        self.config = self.network.store.runtime / 'network-manager-config' / self.network.spec.token
        self.config_fd = None
        self.config_identity = self.config_file_identity = None
        self.netplan = Path('/usr/libexec/netplan/configure').is_file()
        self.unit = OwnedUnit(self.network.store, self.network.spec, 'network-manager', 0,
                              self.network.command, network=self.network.identity)

    def prepare_config(self):
        self.state.verify()
        for name in ('etc', 'lib', 'empty'):
            descriptor = trusted_directory(self.state.path / name, create=True)
            os.close(descriptor)
        if self.netplan:
            program_identity('/usr/libexec/netplan/configure')
            for name in ('etc', 'run', 'lib', 'udev', 'networkd'):
                descriptor = trusted_directory(self.state.path / 'netplan' / name, create=True)
                os.close(descriptor)
        parent = trusted_directory(self.config.parent, create=True)
        try:
            os.mkdir(self.config.name, 0o755, dir_fd=parent)
            self.config_fd = os.open(self.config.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                                     dir_fd=parent)
            os.fchmod(self.config_fd, 0o755)
            info = os.fstat(self.config_fd)
            self.config_identity = info.st_dev, info.st_ino
        finally:os.close(parent)
        atomic_write(self.config_fd, 'NetworkManager.conf', CONFIGURATION)
        info = os.stat('NetworkManager.conf', dir_fd=self.config_fd, follow_symlinks=False)
        self.config_file_identity = info.st_dev, info.st_ino
        self.verify_config()

    def verify_config(self):
        self.state.verify()
        if self.config_fd is None or self.config_identity is None or self.config_file_identity is None:
            raise ServiceError('The private NetworkManager configuration was not captured')
        current = trusted_directory(self.config)
        try:
            info = os.fstat(current)
            if (info.st_dev, info.st_ino) != self.config_identity or os.listdir(current) != ['NetworkManager.conf']:
                raise ServiceError('The private NetworkManager configuration directory changed')
            descriptor = os.open('NetworkManager.conf', os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                                 dir_fd=current)
            try:
                info = os.fstat(descriptor)
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022 or info.st_nlink != 1
                        or (info.st_dev, info.st_ino) != self.config_file_identity
                        or os.read(descriptor, len(CONFIGURATION) + 1) != CONFIGURATION):
                    raise ServiceError('The private NetworkManager configuration changed')
            finally:os.close(descriptor)
        finally:os.close(current)

    def properties(self):
        plan = self.files.plan
        state = str(self.state.path)
        values = ['User=root', 'Group=' + str(plan.gid),
                'NetworkNamespacePath=' + self.network.path,
                'RuntimeDirectory=' + self.runtime, 'RuntimeDirectoryMode=0750', 'UMask=0077',
                'Environment=DBUS_SYSTEM_BUS_ADDRESS=' + plan.bus_address,
                'BindPaths=' + state + '/etc:/etc/NetworkManager ' + state + '/lib:/var/lib/NetworkManager '
                              + str(self.directory) + ':/run/NetworkManager',
                'BindReadOnlyPaths=/run/' + plan.runtime_name + ':/run/systemd/resolve '
                                    '/run/' + plan.bus_name + '/bus:/run/dbus/system_bus_socket',
                'ReadWritePaths=' + state + ' ' + str(self.directory)
                                  + ' /etc/NetworkManager /var/lib/NetworkManager /run/NetworkManager',
                'InaccessiblePaths=-/run/systemd/private -/dev/rfkill '
                                    '-/run/nscd/socket -/run/avahi-daemon/socket '
                                    '-/usr/lib/NetworkManager/dispatcher.d',
                'ProtectSystem=strict', 'PrivateTmp=yes', 'NoNewPrivileges=yes',
                # Physical device management remains on the host. The stock
                # NM platform disables udev when sysfs is read-only, allowing
                # existing namespace-only virtual links to initialize on restart.
                'ReadOnlyPaths=/sys',
                # The stock VPN plugin must terminate its child after that
                # child drops to the distribution's OpenVPN service account.
                'CapabilityBoundingSet=CAP_NET_ADMIN CAP_NET_RAW CAP_NET_BIND_SERVICE '
                                      'CAP_SETUID CAP_SETGID CAP_CHOWN CAP_DAC_READ_SEARCH CAP_KILL']
        if self.netplan:
            # Ubuntu's stock keyfile backend writes YAML and invokes its own
            # generator. Keep all configuration inputs/outputs in this instance;
            # leave the packaged generator executable shared and unchanged.
            mounts = (('etc', '/etc/netplan'), ('run', '/run/netplan'), ('lib', '/usr/lib/netplan'),
                      ('udev', '/run/udev/rules.d'), ('networkd', '/run/systemd/network'))
            for index, value in enumerate(values):
                if value.startswith('BindPaths='):
                    values[index] += ''.join(' ' + state + '/netplan/' + name + ':' + target for name, target in mounts)
                if value.startswith('ReadWritePaths='):
                    values[index] += ''.join(' ' + target for name, target in mounts)
            values.append('ExecStartPre=/usr/libexec/netplan/configure --networkmanager-only')
        return tuple(values)

    def start(self):
        self.network.verify();self.dns.services.verify()
        program_identity('/usr/sbin/NetworkManager')
        self.state.prepare()
        self.prepare_config()
        self.start_prepared()
        from .network_uplink import prepare_uplink
        prepare_uplink(self)
        return self

    def start_prepared(self):
        self.network.verify();self.dns.services.verify();self.verify_config()
        self.unit.start(self.properties(), '--no-daemon', '--config', str(self.config / 'NetworkManager.conf'),
                        '--config-dir', str(self.state.path / 'empty'), '--system-config-dir', str(self.state.path / 'empty'),
                        '--intern-config', str(self.state.path / 'lib/NetworkManager-intern.conf'),
                        '--state-file', str(self.state.path / 'lib/NetworkManager.state'),
                        '--pid-file', str(self.directory / 'NetworkManager.pid'))
        deadline = time.monotonic() + 8
        while True:
            try:self.verify_worker();break
            except (OSError, RuntimeError):
                if self.unit.process.exited(self.unit.pidfd) or time.monotonic() >= deadline:raise
                time.sleep(.05)
        if self.clients:self.publish_client()
        self.verify()
        return self

    def verify_worker(self):
        self.verify_config();self.dns.services.verify();self.unit.verify()
        if not os.statvfs('/proc/' + str(self.unit.identity.pid) + '/root/sys').f_flag & os.ST_RDONLY:
            raise ServiceRetained(self.unit)
        value = self.network.command.run('/usr/bin/busctl', '--address=' + self.files.plan.bus_address,
            '--timeout=2s', 'call', 'org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
            'GetConnectionUnixProcessID', 's', 'org.freedesktop.NetworkManager').split()
        if value != ['u', str(self.unit.identity.pid)]:raise ServiceRetained(self.unit)
        for source, destination in ((self.state.path / 'etc', '/etc/NetworkManager'),
                                    (self.state.path / 'lib', '/var/lib/NetworkManager'),
                                    (self.directory, '/run/NetworkManager'),
                                    (Path('/run') / self.files.plan.bus_name / 'bus', '/run/dbus/system_bus_socket'),
                                    *((self.state.path / 'netplan' / name, target) for name, target in
                                      (('etc', '/etc/netplan'), ('run', '/run/netplan'), ('lib', '/usr/lib/netplan'),
                                       ('udev', '/run/udev/rules.d'), ('networkd', '/run/systemd/network')) if self.netplan)):
            expected = os.stat(source)
            actual = os.stat('/proc/' + str(self.unit.identity.pid) + '/root' + destination)
            if (expected.st_dev, expected.st_ino) != (actual.st_dev, actual.st_ino):
                raise ServiceRetained(self.unit)
        self.unit.verify()

    def client_contents(self):
        from .network_client import identity, serialize
        return serialize({'version':1,'uid':self.network.spec.uid,'gid':self.files.plan.gid,
            'network':list(self.network.identity),'resolver':identity(self.files.plan.runtime_paths[0]),
            'bus':{'directory':identity(self.files.plan.runtime_paths[1]),
                   'socket':identity(Path(self.files.plan.runtime_paths[1])/'bus'),
                   'pid':self.dns.services.bus.identity.pid},
            'worker':self.unit.identity.pid,'invocation':self.unit.identity.invocation})

    def publish_client(self):
        self.verify_worker()
        directory=trusted_directory(self.directory)
        try:atomic_write(directory,'client.json',self.client_contents())
        finally:os.close(directory)

    def verify_client(self):
        directory=trusted_directory(self.directory)
        try:
            fd=os.open('client.json',os.O_RDONLY|os.O_NONBLOCK|os.O_NOFOLLOW|os.O_CLOEXEC,dir_fd=directory)
            try:
                info=os.fstat(fd);contents=self.client_contents()
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_gid != 0
                        or stat.S_IMODE(info.st_mode) != 0o644 or info.st_nlink != 1
                        or os.read(fd,len(contents)+1) != contents):
                    raise ServiceError('The published desktop network-client context changed')
            finally:os.close(fd)
        finally:os.close(directory)

    def verify(self):
        self.verify_worker()
        if self.clients:self.verify_client()

    def nmcli(self, *arguments):
        self.verify()
        result = self.network.command.run('/usr/bin/env', 'DBUS_SYSTEM_BUS_ADDRESS=' + self.files.plan.bus_address,
                                          '/usr/bin/nmcli', '--wait', '5', *arguments)
        self.verify()
        return result

    def close(self):
        self.state.verify()
        self.dns.services.verify()
        self.unit.close()
        if self.directory.exists():raise ServiceRetained(self.unit)
        self.remove_config()
        self.release_handles()

    def remove_config(self, *, partial=False):
        """Partial removal is used only with a recorded configuration intent."""
        if self.config_fd is not None:
            if not partial or os.listdir(self.config_fd):
                self.verify_config()
                os.unlink('NetworkManager.conf', dir_fd=self.config_fd)
            parent = trusted_directory(self.config.parent)
            try:
                info = os.stat(self.config.name, dir_fd=parent, follow_symlinks=False)
                if (info.st_dev, info.st_ino) != self.config_identity:
                    raise ServiceError('The private NetworkManager configuration changed before removal')
                os.rmdir(self.config.name, dir_fd=parent)
            finally:os.close(parent)

    def release_handles(self):
        if self.unit.pidfd is not None:self.unit.process.close(self.unit.pidfd);self.unit.pidfd = None
        if self.config_fd is not None:os.close(self.config_fd);self.config_fd = None
        self.state.release_handles()
