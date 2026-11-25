# SPDX-License-Identifier: GPL-3.0-or-later
"""Own the profile's private DNS units; never adopt an existing service."""
from dataclasses import dataclass
import os
from pathlib import Path
import re
import select
import stat

from .networks import NetworkCommand, NetworkError, NetworkSpec
from .storage import trusted_directory


class ServiceError(RuntimeError):
    pass


class ServiceRetained(ServiceError):
    def __init__(self, unit):
        super().__init__("Private service ownership is uncertain; retain resources: " + unit.name)
        self.unit = unit


def host_controller():
    if os.getuid() != 0 or os.geteuid() != 0:
        raise PermissionError("Private services require the root host controller")
    for kind in ("user", "net", "mnt"):
        left, right = os.stat("/proc/self/ns/" + kind), os.stat("/proc/1/ns/" + kind)
        if (left.st_dev, left.st_ino) != (right.st_dev, right.st_ino):
            raise PermissionError("Private services must be managed from the host namespaces")


@dataclass(frozen=True)
class UnitIdentity:
    invocation: str
    pid: int
    cgroup: str
    start: int


class NativeProcess:
    @staticmethod
    def open(pid):
        return os.pidfd_open(pid)

    @staticmethod
    def exited(fd, seconds=0):
        return bool(select.select([fd], [], [], seconds)[0])

    @staticmethod
    def close(fd):
        os.close(fd)

    @staticmethod
    def read(pid, executable, uid, cgroup, network):
        base = Path("/proc") / str(pid)
        prefix, separator, fields = (base / "stat").read_text().rpartition(") ")
        if not separator or not prefix.startswith(str(pid) + " ("):
            raise ServiceError("Invalid native process start identity")
        values = fields.split()
        if len(values) < 20 or not values[19].isdigit():
            raise ServiceError("Invalid native process start time")
        credentials = [line for line in (base / "status").read_text().splitlines()
                       if line.startswith("Uid:")]
        if len(credentials) != 1 or credentials[0].split()[1:] != [str(uid)] * 4:
            raise ServiceError("Private service credentials changed")
        info = os.stat(base / "exe")
        if (info.st_dev, info.st_ino) != executable:
            raise ServiceError("Private service executable changed")
        if (base / "cgroup").read_text().splitlines() != ["0::" + cgroup]:
            raise ServiceError("Private service cgroup changed")
        if network is not None:
            info = os.stat(base / "ns/net")
            if (info.st_dev, info.st_ino) != network:
                raise ServiceError("Private service network namespace changed")
        return int(values[19])


PROGRAMS = {"dns-bus": "/usr/bin/dbus-daemon",
            "dns-resolver": "/usr/lib/systemd/systemd-resolved",
            "netbird": "/usr/bin/netbird",
            "network-manager": "/usr/sbin/NetworkManager",
            "host-bus": "/usr/bin/xdg-dbus-proxy"}
PROPERTIES = frozenset(("User", "Group", "RuntimeDirectory", "RuntimeDirectoryMode",
    "NoNewPrivileges", "PrivateTmp", "NetworkNamespacePath", "Environment",
    "AmbientCapabilities", "CapabilityBoundingSet", "PrivateDevices", "ProtectSystem",
    "BindPaths", "BindReadOnlyPaths", "InaccessiblePaths", "ReadWritePaths", "ReadOnlyPaths", "UMask", "ExecStartPre",
    "RestrictAddressFamilies", "DevicePolicy", "DeviceAllow", "MemoryMax", "TasksMax", "ProtectHome", "TemporaryFileSystem"))


def program_identity(program):
    path = Path(os.path.realpath(program))
    fd = trusted_directory(path.parent)
    try:
        child = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=fd)
        try:
            info = os.fstat(child)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022 or not info.st_mode & 0o111:
                raise ServiceError("Unsafe private service executable")
            return info.st_dev, info.st_ino
        finally:
            os.close(child)
    finally:
        os.close(fd)


class OwnedUnit:
    def __init__(self, store, spec, role, uid, command=None, *, network=None,
                 process=None, host_guard=host_controller):
        if not isinstance(spec, NetworkSpec) or role not in PROGRAMS:
            raise ValueError("Invalid private service identity")
        if type(uid) is not int:raise ValueError('Invalid private service account')
        if role == 'host-bus':valid_uid = uid == spec.uid
        elif role == 'dns-resolver':valid_uid = 1 <= uid <= 999
        else:valid_uid = uid == 0
        if not valid_uid:
            raise ValueError("Invalid private service account")
        if network is not None and (not isinstance(network, tuple) or len(network) != 2
                or any(type(value) is not int or value <= 0 for value in network)):
            raise ValueError("Invalid private service namespace identity")
        self.store, self.spec, self.role, self.uid = store, spec, role, uid
        self.name = "karyo-" + role + "-" + spec.token + ".service"
        self.program = PROGRAMS[role]
        self.command = NetworkCommand() if command is None else command
        self.process = NativeProcess() if process is None else process
        self.host_guard, self.network = host_guard, network
        self.identity = None
        self.executable = None
        self.pidfd = None
        self.attempted = False
        self.closed = False

    def guard(self):
        self.host_guard()
        if not self.store._locked or self.closed:
            raise PermissionError("Private service requires its held configuration lock")

    def info(self):
        output = self.command.run("/usr/bin/systemctl", "show", self.name, "--no-pager",
            "--property=LoadState", "--property=MainPID", "--property=InvocationID",
            "--property=ControlGroup", "--property=Transient", "--property=ActiveState")
        rows = {}
        for line in output.splitlines():
            key, separator, value = line.partition("=")
            if not separator or key in rows:
                raise ServiceError("Ambiguous native unit inventory")
            rows[key] = value
        required = {"LoadState", "MainPID", "InvocationID", "ControlGroup", "Transient", "ActiveState"}
        if set(rows) != required or rows["LoadState"] not in {"loaded", "not-found"}:
            raise ServiceError("Incomplete native unit inventory")
        return rows

    @staticmethod
    def properties(values):
        if not isinstance(values, (tuple, list)) or len(values) > 32:
            raise ValueError("Invalid private service properties")
        found = set()
        for value in values:
            if not isinstance(value, str) or not value or len(value) > 4096 or any(c in value for c in "\0\r\n"):
                raise ValueError("Invalid private service property")
            key, separator, unused = value.partition("=")
            if not separator or key not in PROPERTIES or key in found:
                raise ValueError("Unsupported or repeated private service property")
            found.add(key)
        if not {"User", "Group"} <= found:
            raise ValueError("Private services require explicit credentials")
        return tuple(values)

    def capture_executable(self):
        return program_identity(self.program)

    def execution_command(self, arguments):
        return (self.program, *arguments)

    def stop_seconds(self):
        return 5

    def start(self, properties, *arguments):
        self.guard()
        if self.attempted:
            raise ServiceRetained(self)
        properties = self.properties(properties)
        if len(arguments) > 16 or any(not isinstance(arg, str) or len(arg) > 4096
                or any(c in arg for c in "\0\r\n") for arg in arguments):
            raise ValueError("Invalid private service arguments")
        if self.info()["LoadState"] != "not-found":
            raise ServiceError("Refuse an existing private service: " + self.name)
        self.executable = self.capture_executable()
        options = ["/usr/bin/systemd-run", "--quiet", "--collect", "--unit=" + self.name,
            "--property=Type=exec", "--property=Restart=no", "--property=KillMode=control-group",
            "--property=TimeoutStopSec=" + str(self.stop_seconds()) + "s"]
        options.extend("--property=" + value for value in properties)
        self.attempted = True
        try:
            self.command.run(*options, "--", *self.execution_command(arguments))
            rows = self.info()
            if (rows["Transient"] != "yes" or rows["ActiveState"] != "active"
                    or re.fullmatch("[a-f0-9]{32}", rows["InvocationID"]) is None
                    or not rows["MainPID"].isdigit() or int(rows["MainPID"]) <= 1
                    or rows["ControlGroup"] != "/system.slice/" + self.name):
                raise ServiceError("Fresh private service identity was not captured")
            pid = int(rows["MainPID"])
            self.pidfd = self.process.open(pid)
            start = self.process.read(pid, self.executable, self.uid, rows["ControlGroup"], self.network)
            self.identity = UnitIdentity(rows["InvocationID"], pid, rows["ControlGroup"], start)
            self.verify()
            return self
        except BaseException as error:
            raise ServiceRetained(self) from error

    def verify(self):
        self.guard()
        if self.identity is None or self.pidfd is None:
            raise ServiceRetained(self)
        rows, identity = self.info(), self.identity
        if (rows["LoadState"] != "loaded" or rows["Transient"] != "yes"
                or rows["ActiveState"] != "active" or rows["InvocationID"] != identity.invocation
                or rows["MainPID"] != str(identity.pid) or rows["ControlGroup"] != identity.cgroup
                or self.process.exited(self.pidfd)):
            raise ServiceRetained(self)
        start = self.process.read(identity.pid, self.executable, self.uid, identity.cgroup, self.network)
        if start != identity.start:
            raise ServiceRetained(self)
        if self.info() != rows:
            raise ServiceRetained(self)

    def cgroup_pids(self):
        if self.identity is None:raise ServiceRetained(self)
        root = Path('/sys/fs/cgroup' + self.identity.cgroup)
        if not root.exists():return set()
        values = set()
        for path in (root/'cgroup.procs', *root.rglob('*/cgroup.procs')):
            for value in path.read_text().splitlines():
                if not value.isdecimal() or str(int(value)) != value or int(value) <= 1:
                    raise ServiceRetained(self)
                values.add(int(value))
        return values

    def consumer_pids(self):
        """Capture the whole owned worker cgroup, including upstream plugins."""
        for attempt in range(3):
            self.verify()
            try:
                values = self.cgroup_pids()
                if self.identity.pid not in values:raise ProcessLookupError()
                for pid in values:
                    descriptor = os.pidfd_open(pid)
                    try:
                        if select.select([descriptor], [], [], 0)[0]:raise ProcessLookupError()
                        base = Path('/proc', str(pid))
                        before = (base/'stat').read_text()
                        groups = (base/'cgroup').read_text().splitlines()
                        prefix = '0::' + self.identity.cgroup
                        if len(groups) != 1 or not (groups[0] == prefix or groups[0].startswith(prefix + '/')):
                            raise ServiceRetained(self)
                        if self.network is not None:
                            info = os.stat(base/'ns/net')
                            if (info.st_dev, info.st_ino) != self.network:raise ServiceRetained(self)
                        # Only starttime is stable; CPU/scheduling fields may change.
                        start = before.rpartition(') ')[2].split()[19]
                        if ((base/'stat').read_text().rpartition(') ')[2].split()[19] != start
                                or select.select([descriptor], [], [], 0)[0]):raise ProcessLookupError()
                    finally:os.close(descriptor)
                self.verify()
                if self.cgroup_pids() != values:raise ProcessLookupError()
                return values
            except (FileNotFoundError, ProcessLookupError):
                if attempt == 2:raise ServiceRetained(self) from None

    def close(self):
        if self.closed:
            return
        self.guard()
        if not self.attempted:
            self.closed = True
            return
        if self.identity is None or self.pidfd is None:
            raise ServiceRetained(self)
        rows, identity = self.info(), self.identity
        if rows["LoadState"] == "not-found":
            if not self.process.exited(self.pidfd):
                raise ServiceRetained(self)
        else:
            if (rows["Transient"] != "yes" or rows["InvocationID"] != identity.invocation
                    or rows["MainPID"] not in {"0", str(identity.pid)}
                    or rows["ControlGroup"] not in {"", identity.cgroup}):
                raise ServiceRetained(self)
            if not self.process.exited(self.pidfd):
                self.verify()
            self.command.run("/usr/bin/systemctl", "--job-mode=fail", "stop", self.name)
            rows = self.info()
            if rows["LoadState"] != "not-found":
                if (rows["Transient"] != "yes" or rows["InvocationID"] != identity.invocation
                        or rows["MainPID"] != "0" or rows["ActiveState"] not in {"inactive", "failed"}
                        or rows['ControlGroup']):
                    raise ServiceRetained(self)
        if not self.process.exited(self.pidfd, 5):
            raise ServiceRetained(self)
        if self.cgroup_pids():raise ServiceRetained(self)
        self.process.close(self.pidfd)
        self.pidfd = None
        self.closed = True


class ResolverServices:
    """Consume prepared resolver files; retain leases on uncertain native state."""
    def __init__(self, files, command=None):
        self.files = files
        self.command = files.network.command if command is None else command
        network = files.network
        self.bus = OwnedUnit(network.store, network.spec, "dns-bus", 0, self.command)
        self.resolver = OwnedUnit(network.store, network.spec, "dns-resolver",
            files.plan.service_uid, self.command, network=network.identity)

    @property
    def closed(self):
        return self.bus.closed and self.resolver.closed

    def verify(self):
        self.files.verify()
        self.bus.verify()
        self.resolver.verify()

    def start(self, *, before=None, after=None):
        from .dns import seed_private_dns
        self.files.network.verify()
        self.files.network._empty()
        plan = self.files.plan
        self.files.verify()
        if before is not None:
            before("bus")
        self.bus.start(plan.bus_properties(), "--nofork", "--nopidfile", "--nosyslog",
            "--config-file=" + plan.config + "/bus.conf")
        if after is not None:
            after("bus")
        self.wait_for_bus()
        if before is not None:
            before("resolver")
        self.resolver.start(plan.resolver_properties())
        if after is not None:
            after("resolver")
        import json
        import time
        deadline = time.monotonic() + 5
        while True:
            self.bus.verify()
            self.resolver.verify()
            try:
                value = json.loads(self.command.run("/usr/bin/busctl", "--address=" + plan.bus_address,
                    "--json=short", "--timeout=1s", "call", "org.freedesktop.DBus",
                    "/org/freedesktop/DBus", "org.freedesktop.DBus",
                    "GetConnectionUnixProcessID", "s", "org.freedesktop.resolve1"))
            except NetworkError:
                if time.monotonic() >= deadline:
                    raise ServiceRetained(self.resolver)
                time.sleep(0.05)
                continue
            if value != {"type": "u", "data": [self.resolver.identity.pid]}:
                raise ServiceRetained(self.resolver)
            break
        seed_private_dns(self.files)
        self.bus.verify()
        self.resolver.verify()
        return self

    def wait_for_bus(self):
        import time
        deadline = time.monotonic() + 5
        while True:
            self.bus.verify()
            try:
                self.command.run("/usr/bin/busctl", "--address=" + self.files.plan.bus_address,
                    "--timeout=1s", "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                    "org.freedesktop.DBus.Peer", "Ping")
                return
            except NetworkError:
                if time.monotonic() >= deadline:
                    raise ServiceRetained(self.bus)
                time.sleep(0.05)

    def close(self, *, before=None, after=None):
        if self.closed:
            return
        self.files.network.verify()
        # A DNS shutdown must not silently strand a live desktop or VPN client.
        expected = set() if self.resolver.identity is None else {str(self.resolver.identity.pid)}
        consumers = self.files.network._ip("netns", "pids", self.files.network.name).splitlines()
        if set(consumers) - expected:
            raise ServiceRetained(self.resolver)
        for role, unit in (("resolver", self.resolver), ("bus", self.bus)):
            if not unit.closed:
                if before is not None:
                    before(role)
                unit.close()
                if after is not None:
                    after(role)
            self.files.network._empty()
