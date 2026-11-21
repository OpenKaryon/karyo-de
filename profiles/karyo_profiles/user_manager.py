# SPDX-License-Identifier: GPL-3.0-or-later
"""Scope a stopped profile's native user manager without replacing a live one."""
import json
import os
from pathlib import Path
import re
import stat

from .logind import LoginOwner, Sessions
from .services import NativeProcess, ServiceError, UnitIdentity, host_controller, program_identity
from .storage import atomic_write, trusted_directory


class ManagerPending(ServiceError):
    """The captured manager/logind lifetime has not ended yet."""


class ManagerRetained(ServiceError):
    def __init__(self, scope):
        super().__init__("User-manager ownership is uncertain; retain profile resources: " + scope.name)
        self.scope = scope


def uid_processes(uid):
    found = []
    for path in Path("/proc").iterdir():
        if not path.name.isdecimal():
            continue
        try:
            with (path / "status").open() as stream:
                lines = stream.read(256 * 1024).splitlines()
            rows = [line.split()[1:] for line in lines if line.startswith("Uid:")]
            if len(rows) != 1 or len(rows[0]) != 4 or any(not value.isdecimal() for value in rows[0]):
                raise ServiceError("Invalid native process credentials")
            if str(uid) in rows[0]:
                found.append(int(path.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return tuple(found)


BASE_MANAGER = ("/usr/lib/systemd/systemd", ("/usr/lib/systemd/systemd", "--user"), False)
SCOPED_MANAGER = ("/usr/bin/setpriv", ("/usr/bin/setpriv", "--inh-caps=-all",
                  "--ambient-caps=-all", "/usr/lib/systemd/systemd", "--user"), False)


def manager_exec_start(command, uid):
    if type(uid) is not int or not 1000 <= uid < 65534:
        raise ValueError("Invalid user-manager UID")
    prefix = ("/usr/bin/busctl", "--system", "--json=short", "--timeout=5s")
    obj = "/org/freedesktop/systemd1/unit/user_40" + str(uid) + "_2eservice"
    value = json.loads(command.run(*prefix, "call", "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "LoadUnit", "s",
        "user@" + str(uid) + ".service"))
    if value != {"type": "o", "data": [obj]}:
        raise ServiceError("Unexpected native user-manager object")
    value = json.loads(command.run(*prefix, "get-property", "org.freedesktop.systemd1",
                                   obj, "org.freedesktop.systemd1.Service", "ExecStart"))
    if (not isinstance(value, dict) or set(value) != {"type", "data"}
            or value["type"] != "a(sasbttttuii)" or not isinstance(value["data"], list)
            or len(value["data"]) != 1 or not isinstance(value["data"][0], list)
            or len(value["data"][0]) != 10):
        raise ServiceError("Unexpected native user-manager command schema")
    row = value["data"][0]
    if (not isinstance(row[0], str) or not isinstance(row[1], list)
            or not 1 <= len(row[1]) <= 16 or type(row[2]) is not bool
            or any(not isinstance(arg, str) or len(arg) > 4096 or any(c in arg for c in "\0\r\n") for arg in row[1])
            or any(type(arg) is not int or not 0 <= arg < 2**64 for arg in row[3:7])
            or type(row[7]) is not int or not 0 <= row[7] < 2**32
            or any(type(arg) is not int or not -(2**31) <= arg < 2**31 for arg in row[8:])):
        raise ServiceError("Invalid native user-manager command record")
    return row[0], tuple(row[1]), row[2]


class UserManagerScope:
    def __init__(self, files, *, sessions=None, command=None, process=None, netbird=False):
        if type(netbird) is not bool:
            raise ValueError("The provider launch policy must be explicit")
        self.files, self.network = files, files.network
        self.environment = (("KARYO_NETBIRD_ENDPOINT=/run/karyo-netbird-" + self.network.spec.token + "/client.json",)
                            if netbird else ())
        self.command = self.network.command if command is None else command
        self.sessions = Sessions() if sessions is None else sessions
        self.process = NativeProcess() if process is None else process
        self.account = files.accounts.by_uid(self.network.spec.uid)
        self.name = "user@" + str(self.account.uid) + ".service"
        self.directory = Path("/run/systemd/system") / (self.name + ".d")
        self.filename = "90-karyo-profile.conf"
        self.contents = ("# Owned by the current Karyo profile runtime.\n[Service]\n"
                         + "\n".join(files.plan.client_properties()) + "\nExecStart=\nExecStart="
                         + " ".join(SCOPED_MANAGER[1]) + "\n"
                         + ("Environment=" + " ".join(self.environment) + "\n" if self.environment else "")).encode()
        self.fd = None
        self.directory_identity = self.file_identity = None
        self.initial_dropins = None
        self.identity = self.login = self.manager_login = None
        self.executable = self.pidfd = None
        self.created = False
        self.removed = False
        self.closed = False

    def guard(self):
        host_controller()
        self.files.verify()
        if self.closed or self.files.accounts.by_uid(self.account.uid) != self.account:
            raise ManagerRetained(self)

    def info(self):
        keys = ("LoadState", "ActiveState", "SubState", "MainPID", "InvocationID",
                "ControlGroup", "DropInPaths", "NetworkNamespacePath")
        value = self.command.run("/usr/bin/systemctl", "show", self.name, "--no-pager",
                                 *("--property=" + key for key in keys))
        rows = {}
        for line in value.splitlines():
            key, separator, data = line.partition("=")
            if not separator or key not in keys or key in rows:
                raise ServiceError("Ambiguous native user-manager state")
            rows[key] = data
        if set(rows) != set(keys) or rows["LoadState"] != "loaded":
            raise ServiceError("Native user-manager template is unavailable")
        return rows

    @staticmethod
    def inactive(rows):
        return (rows["ActiveState"] == "inactive" and rows["SubState"] == "dead"
                and rows["MainPID"] == "0" and rows["InvocationID"] == "" and rows["ControlGroup"] == "")

    @staticmethod
    def dropins(rows):
        paths = tuple(rows["DropInPaths"].split())
        if any(not path.startswith("/") or "\\" in path or "%" in path for path in paths):
            raise ServiceError("Unsupported native user-manager drop-in path")
        if len(set(paths)) != len(paths):
            raise ServiceError("Duplicate native user-manager drop-in path")
        return frozenset(paths)

    def prepare(self):
        self.guard()
        if self.created or self.fd is not None or self.removed:
            raise ManagerRetained(self)
        rows = self.info()
        if (not self.inactive(rows) or rows["NetworkNamespacePath"]
                or self.sessions.owners(self.account.uid) or uid_processes(self.account.uid)
                or os.path.lexists("/run/user/" + str(self.account.uid))
                or os.path.lexists("/var/lib/systemd/linger/" + self.account.name)
                or os.path.lexists("/etc/systemd/system/" + self.name + ".d")):
            raise ServiceError("The selected profile already has a login, process, runtime or private manager policy")
        if manager_exec_start(self.command, self.account.uid) != BASE_MANAGER:
            raise ServiceError("Unsupported user-manager command; existing policy was not replaced")
        program_identity("/usr/bin/setpriv")
        self.initial_dropins = self.dropins(rows)
        parent = trusted_directory(self.directory.parent)
        try:
            os.mkdir(self.directory.name, 0o755, dir_fd=parent)
            self.created = True
            self.fd = os.open(self.directory.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                              dir_fd=parent)
            os.fchmod(self.fd, 0o755)
            info = os.fstat(self.fd)
            self.directory_identity = info.st_dev, info.st_ino
        finally:
            os.close(parent)
        atomic_write(self.fd, self.filename, self.contents)
        info = os.stat(self.filename, dir_fd=self.fd, follow_symlinks=False)
        self.file_identity = info.st_dev, info.st_ino
        self.command.run("/usr/bin/systemctl", "daemon-reload")
        self.verify_file()
        self.verify_policy(self.info())
        return self

    def verify_file(self):
        if self.fd is None or self.removed or self.file_identity is None:
            raise ManagerRetained(self)
        current = trusted_directory(self.directory)
        try:
            info = os.fstat(current)
            if (info.st_dev, info.st_ino) != self.directory_identity or os.listdir(current) != [self.filename]:
                raise ManagerRetained(self)
            fd = os.open(self.filename, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=current)
            try:
                info = os.fstat(fd)
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022
                        or info.st_nlink != 1 or (info.st_dev, info.st_ino) != self.file_identity
                        or os.read(fd, len(self.contents) + 1) != self.contents):
                    raise ManagerRetained(self)
            finally:
                os.close(fd)
        finally:
            os.close(current)

    def verify_policy(self, rows):
        expected = self.initial_dropins | {str(self.directory / self.filename)}
        if (self.dropins(rows) != expected or rows["NetworkNamespacePath"] != self.network.path
                or manager_exec_start(self.command, self.account.uid) != SCOPED_MANAGER):
            raise ManagerRetained(self)

    def capture(self, login):
        self.guard()
        self.verify_file()
        if self.identity is not None or not isinstance(login, LoginOwner):
            raise ManagerRetained(self)
        rows = self.info()
        self.verify_policy(rows)
        root = "/user.slice/user-" + str(self.account.uid) + ".slice/" + self.name
        if (rows["ActiveState"] != "active" or rows["SubState"] != "running"
                or rows["ControlGroup"] != root or not rows["MainPID"].isdecimal()
                or int(rows["MainPID"]) <= 1 or re.fullmatch("[a-f0-9]{32}", rows["InvocationID"]) is None
                or login.uid != self.account.uid or login.name != self.account.name
                or login.kind != "user" or not login.scope or login.leader <= 1):
            raise ManagerRetained(self)
        pid = int(rows["MainPID"])
        owners = self.sessions.owners(self.account.uid)
        managers = [owner for owner in owners if owner.kind in {"manager", "manager-early"}
                    and owner.leader == pid and owner.name == self.account.name
                    and owner.service == "systemd-user" and not owner.scope]
        if (len(owners) != 2 or len(managers) != 1
                or not any(owner.identity == login.identity and owner.state in {"active", "online"} for owner in owners)):
            raise ManagerRetained(self)
        self.executable = program_identity("/usr/lib/systemd/systemd")
        self.pidfd = self.process.open(pid)
        start = self.process.read(pid, self.executable, self.account.uid, root + "/init.scope", self.network.identity)
        self.identity = UnitIdentity(rows["InvocationID"], pid, root + "/init.scope", start)
        self.login, self.manager_login = login, managers[0]
        self.verify()
        return self

    def verify_manager(self, rows):
        identity = self.identity
        if (identity is None or self.pidfd is None or rows["MainPID"] != str(identity.pid)
                or rows["InvocationID"] != identity.invocation or rows["ActiveState"] != "active"
                or rows["ControlGroup"] + "/init.scope" != identity.cgroup
                or self.process.exited(self.pidfd)):
            raise ManagerRetained(self)
        start = self.process.read(identity.pid, self.executable, self.account.uid, identity.cgroup, self.network.identity)
        if start != identity.start:
            raise ManagerRetained(self)
        private = os.stat("/run/" + self.files.plan.runtime_name)
        mounted = os.stat("/proc/" + str(identity.pid) + "/root/run/systemd/resolve")
        if (private.st_dev, private.st_ino) != (mounted.st_dev, mounted.st_ino):
            raise ManagerRetained(self)

    def verify(self):
        self.guard()
        self.verify_file()
        rows = self.info()
        self.verify_policy(rows)
        self.verify_manager(rows)
        owners = self.sessions.owners(self.account.uid)
        expected = {self.login.identity, self.manager_login.identity}
        if len(owners) != 2 or {owner.identity for owner in owners} != expected:
            raise ManagerRetained(self)
        if self.info() != rows:
            raise ManagerRetained(self)

    def close(self):
        if self.closed:
            return
        if not self.created and self.fd is None:
            self.closed = True
            return
        self.guard()
        rows = self.info()
        # Do not stop user@UID: logind owns its lifetime. A concurrent SSH/TTY
        # login or lingering manager must keep its processes and our network.
        if not self.inactive(rows):
            if self.identity is None:
                raise ManagerRetained(self)
            if rows["ActiveState"] == "deactivating":
                root = self.identity.cgroup.removesuffix("/init.scope")
                if (rows["InvocationID"] != self.identity.invocation
                        or rows["MainPID"] not in {"0", str(self.identity.pid)}
                        or rows["ControlGroup"] not in {"", root}):
                    raise ManagerRetained(self)
            else:
                self.verify_manager(rows)
            raise ManagerPending("Waiting for the captured logind user manager to finish")
        if ((self.pidfd is not None and not self.process.exited(self.pidfd))
                or self.sessions.owners(self.account.uid) or uid_processes(self.account.uid)):
            raise ManagerPending("Waiting for the profile's native login/process lifetime")
        if not self.removed:
            self.verify_file()
            self.verify_policy(rows)
            os.unlink(self.filename, dir_fd=self.fd)
            parent = trusted_directory(self.directory.parent)
            try:
                info = os.stat(self.directory.name, dir_fd=parent, follow_symlinks=False)
                if (info.st_dev, info.st_ino) != self.directory_identity:
                    raise ManagerRetained(self)
                os.rmdir(self.directory.name, dir_fd=parent)
            finally:
                os.close(parent)
            self.removed = True
        self.command.run("/usr/bin/systemctl", "daemon-reload")
        rows = self.info()
        if (not self.inactive(rows) or self.dropins(rows) != self.initial_dropins
                or rows["NetworkNamespacePath"] or self.sessions.owners(self.account.uid)
                or uid_processes(self.account.uid)
                or manager_exec_start(self.command, self.account.uid) != BASE_MANAGER):
            raise ManagerRetained(self)
        if self.pidfd is not None:
            self.process.close(self.pidfd)
            self.pidfd = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
        self.closed = True
