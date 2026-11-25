# SPDX-License-Identifier: GPL-3.0-or-later
"""Own a fresh graphical PAM login and its distinct logind session scope."""
from dataclasses import dataclass
import os
from pathlib import Path
import re
import time

from .consoles import DESKTOP_VTS
from .logind import Sessions, SessionError
from .services import NativeProcess, ServiceError, host_controller, program_identity
from .switching import ExistingSwitcher
from .user_manager import uid_processes


class GraphicalRetained(ServiceError):
    def __init__(self, lease):
        super().__init__("Graphical session ownership is uncertain; retain resources: " + lease.name)
        self.lease = lease


class GraphicalPending(ServiceError):
    pass


@dataclass(frozen=True)
class GraphicalIdentity:
    invocation: str
    pid: int
    start: int
    login: object
    scope_invocation: str
    cgroup: str


def finish_abandoned_login(identity, sessions, command, *, exited=None):
    """Finish only a captured closing login whose original leader has exited."""
    host_controller()
    owners = [owner for owner in sessions.owners(identity.login.uid) if owner.id == identity.login.id]
    if not owners:
        return
    if len(owners) != 1 or owners[0].identity != identity.login.identity:
        raise ServiceError("The closing graphical login identity changed")
    if owners[0].state != "closing":
        return
    if exited is None:
        from .journal_dns import process_gone
        exited = lambda: process_gone(identity.pid, identity.start)
    if not exited():
        return
    # With KillUserProcesses=no logind can abandon a scope at leader exit.
    # TerminateSession becomes a no-op once that session is already stopping.
    keys = ("LoadState", "Transient", "InvocationID", "ControlGroup")
    text = command.run("/usr/bin/systemctl", "show", identity.login.scope, "--no-pager",
                       *("--property=" + key for key in keys))
    rows = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator or key not in keys or key in rows:
            raise ServiceError("Ambiguous closing graphical scope inventory")
        rows[key] = value
    if rows != dict(LoadState="loaded", Transient="yes", InvocationID=identity.scope_invocation,
                    ControlGroup=identity.cgroup):
        raise ServiceError("The captured closing graphical scope changed")
    command.run("/usr/bin/systemctl", "--job-mode=fail", "--no-block", "stop", identity.login.scope)


class GraphicalSession:
    ENTRY = "/usr/libexec/karyo/profile-session-entry"
    WAIT = "/usr/libexec/karyo/profile-session-wait"

    def __init__(self, manager, *, sessions=None, command=None, process=None, host_bus=None,
                 clock=time.monotonic, sleep=time.sleep):
        self.manager, self.files = manager, manager.files
        self.host_bus = host_bus
        self.network, self.account = manager.network, manager.account
        self.store = self.network.store
        self.command = self.network.command if command is None else command
        self.sessions = Sessions() if sessions is None else sessions
        self.process = NativeProcess() if process is None else process
        self.clock, self.sleep = clock, sleep
        self.name = "karyo-session-" + self.network.spec.token + ".service"
        self.identity = None
        self.invocation = self.pid = self.pidfd = self.vt = None
        self.compositor = self.waiter = None
        self.attempted = False
        self.entered = False
        self.closed = False

    def guard(self):
        host_controller()
        self.files.verify()
        if self.closed or self.files.accounts.by_uid(self.account.uid) != self.account:
            raise GraphicalRetained(self)

    def info(self, scope=None):
        name = self.name if scope is None else scope
        keys = ["LoadState", "InvocationID", "ControlGroup", "ActiveState", "Transient"]
        if scope is None:
            keys.append("MainPID")
        result = self.command.run("/usr/bin/systemctl", "show", name, "--no-pager",
                                  *("--property=" + key for key in keys))
        rows = {}
        for line in result.splitlines():
            key, separator, value = line.partition("=")
            if not separator or key not in keys or key in rows:
                raise ServiceError("Ambiguous graphical unit inventory")
            rows[key] = value
        if set(rows) != set(keys) or rows["LoadState"] not in {"loaded", "not-found"}:
            raise ServiceError("Incomplete graphical unit inventory")
        return rows

    def login(self):
        if self.identity is None:
            raise GraphicalRetained(self)
        values = [value for value in self.sessions.owners(self.account.uid)
                  if value.id == self.identity.login.id]
        if not values:
            return None
        if len(values) != 1 or values[0].identity != self.identity.login.identity:
            raise GraphicalRetained(self)
        return values[0]

    def process_start(self, cgroup):
        info = os.stat("/proc/" + str(self.pid) + "/exe")
        return self.process.read(self.pid, (info.st_dev, info.st_ino),
                                 self.account.uid, cgroup, self.network.identity)

    def start(self, console):
        self.guard()
        if self.attempted or console.store is not self.store or console.vt not in DESKTOP_VTS:
            raise GraphicalRetained(self)
        console.verify()
        self.manager.verify_file()
        rows = self.manager.info()
        self.manager.verify_policy(rows)
        allowed = set()
        if getattr(self.manager,'host_bus_view',False) is True:
            if self.host_bus is None or self.host_bus.dns.files is not self.files:
                raise ServiceError('The graphical host-service bus was not captured for this manager')
            self.host_bus.verify()
            allowed = self.host_bus.scope.unit.consumer_pids()
        elif self.host_bus is not None:raise ServiceError('An uncaptured graphical host-service bus was supplied')
        if (not self.manager.inactive(rows) or self.sessions.owners(self.account.uid)
                or set(uid_processes(self.account.uid)) - allowed):
            raise ServiceError("A graphical profile requires its prepared offline user manager")
        expiry = self.files.accounts.expiry_day(self.account.name)
        hashed = self.files.accounts.password_hash(self.account.name)
        if ((expiry is not None and expiry <= int(time.time() // 86400))
                or not hashed or hashed.startswith(("!", "*"))):
            raise PermissionError("The selected profile is locked or expired")
        home = Path(self.account.home)
        if not home.is_absolute() or ".." in home.parts or any(c in str(home) for c in "\0\r\n%"):
            raise ValueError("Unsupported graphical profile home directory")
        if self.info()["LoadState"] != "not-found":
            raise ServiceError("Refuse an existing graphical profile unit")
        for program in (self.ENTRY, self.WAIT, "/usr/bin/karyo-session", "/usr/bin/setpriv"):
            program_identity(program)
        self.compositor = program_identity("/usr/bin/labwc")
        self.waiter = program_identity("/usr/bin/python3")
        self.vt = console.vt
        projection = (self.host_bus.scope.client_properties() if self.host_bus is not None
                      else self.files.plan.client_properties())
        properties = ("Type=exec", "Restart=no", "KillMode=control-group", "TimeoutStopSec=5s",
            "User=" + str(self.account.uid), "Group=" + str(self.account.gid),
            "PAMName=karyo-desktop", "TTYPath=" + console.path, "StandardInput=tty-fail",
            "StandardOutput=journal", "StandardError=journal", "TTYReset=no", "TTYVHangup=no",
            "TTYVTDisallocate=no", "WorkingDirectory=" + self.account.home,
            *projection)
        environment = ("XDG_SESSION_TYPE=wayland", "XDG_SESSION_CLASS=user", "XDG_SEAT=seat0",
            "XDG_VTNR=" + str(self.vt), "HOME=" + self.account.home, "USER=" + self.account.name,
            "LOGNAME=" + self.account.name, "PATH=/usr/local/bin:/usr/bin:/bin",
            "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/" + str(self.account.uid) + "/bus",
            *self.manager.environment)
        args = ["/usr/bin/systemd-run", "--quiet", "--collect", "--unit=" + self.name]
        args.extend("--property=" + value for value in properties)
        args.extend("--setenv=" + value for value in environment)
        self.attempted = True
        try:
            self.command.run(*args, "--", self.ENTRY)
            rows = self.info()
            if (rows["LoadState"] != "loaded" or rows["Transient"] != "yes" or rows["ActiveState"] != "active"
                    or re.fullmatch("[a-f0-9]{32}", rows["InvocationID"]) is None
                    or not rows["MainPID"].isdecimal() or int(rows["MainPID"]) <= 1
                    or rows["ControlGroup"] != "/system.slice/" + self.name):
                raise GraphicalRetained(self)
            self.invocation, self.pid = rows["InvocationID"], int(rows["MainPID"])
            self.pidfd = self.process.open(self.pid)
            deadline = self.clock() + 8
            while True:
                if self.process.exited(self.pidfd):
                    raise GraphicalRetained(self)
                candidates = [value for value in self.sessions.owners(self.account.uid)
                              if value.leader == self.pid and value.kind == "user"]
                if candidates:
                    if len(candidates) != 1:
                        raise GraphicalRetained(self)
                    owner = candidates[0]
                    if owner.name != self.account.name or owner.service != "karyo-desktop" or not owner.scope:
                        raise GraphicalRetained(self)
                    cgroup = "/user.slice/user-" + str(self.account.uid) + ".slice/" + owner.scope
                    scope = self.info(owner.scope)
                    if (scope["LoadState"] == "loaded" and scope["Transient"] == "yes"
                            and scope["ControlGroup"] == cgroup
                            and re.fullmatch("[a-f0-9]{32}", scope["InvocationID"])):
                        self.identity = GraphicalIdentity(self.invocation, self.pid, self.process_start(cgroup),
                                                         owner, scope["InvocationID"], cgroup)
                        break
                if self.clock() >= deadline:
                    raise GraphicalRetained(self)
                self.sleep(0.05)
            current = self.sessions.get(self.identity.login.id)
            if (current is None or current.uid != self.account.uid or current.name != self.account.name
                    or current.vt != self.vt or current.active):
                raise GraphicalRetained(self)
            self.manager.capture(self.identity.login)
            self.verify()
            if not self.waiting():
                raise GraphicalRetained(self)
            return self
        except BaseException as error:
            if isinstance(error, GraphicalRetained):
                raise
            raise GraphicalRetained(self) from error

    def verify(self):
        self.guard()
        if self.identity is None or self.pidfd is None:
            raise GraphicalRetained(self)
        if self.host_bus is not None:
            self.host_bus.verify()
            node = os.stat('/proc/' + str(self.pid) + '/root/run/dbus/system_bus_socket')
            if (node.st_dev,node.st_ino) != self.host_bus.scope.socket_identity:raise GraphicalRetained(self)
        rows, identity = self.info(), self.identity
        if (rows["LoadState"] != "loaded" or rows["Transient"] != "yes" or rows["ActiveState"] != "active"
                or rows["InvocationID"] != identity.invocation or rows["MainPID"] != str(identity.pid)
                or rows["ControlGroup"] != "/system.slice/" + self.name
                or self.process.exited(self.pidfd) or self.process_start(identity.cgroup) != identity.start
                or self.login() is None):
            raise GraphicalRetained(self)
        scope = self.info(identity.login.scope)
        if (scope["LoadState"] != "loaded" or scope["Transient"] != "yes"
                or scope["InvocationID"] != identity.scope_invocation or scope["ControlGroup"] != identity.cgroup):
            raise GraphicalRetained(self)
        self.manager.verify()
        if self.info() != rows:
            raise GraphicalRetained(self)

    def waiting(self):
        self.verify()
        info = os.stat("/proc/" + str(self.pid) + "/exe")
        args = Path("/proc/" + str(self.pid) + "/cmdline").read_bytes().split(b"\0")
        return ((info.st_dev, info.st_ino) == self.waiter
                and args == [b"/usr/bin/python3", b"-I", self.WAIT.encode(), b""])

    def compositor_ready(self):
        self.verify()
        info = os.stat("/proc/" + str(self.pid) + "/exe")
        return (info.st_dev, info.st_ino) == self.compositor

    def enter(self, caller, *, reauthorize=None):
        self.guard()
        if reauthorize is not None and reauthorize()!=caller:
            raise PermissionError("The graphical session caller changed")
        family = self.store.ownership().family(caller.name)
        if self.account.name not in family or self.files.accounts.by_uid(caller.uid) != caller:
            raise PermissionError("Only a desktop in the active caller's family may be entered")
        source = ExistingSwitcher.single(self.sessions.all(), caller, active=True)
        if self.entered or not self.waiting():
            raise GraphicalRetained(self)
        destination = self.sessions.get(self.identity.login.id)
        if destination is None or destination.active or destination.vt != self.vt:
            raise GraphicalRetained(self)
        switcher = ExistingSwitcher(self.store, accounts=self.files.accounts, sessions=self.sessions,
                                    clock=self.clock, sleep=self.sleep)
        deadline = self.clock() + 8
        if self.store.settings(caller.name).password != "none":
            switcher.lock_ready(source, deadline)
        current = switcher.read_same(source, deadline)
        if not current.active:
            raise SessionError("The active source changed before entering the profile")
        self.verify()
        if reauthorize is not None and reauthorize()!=caller:
            raise PermissionError("The graphical session caller changed before activation")
        self.sessions.activate(self.identity.login.id, timeout=switcher.remaining(deadline))
        self.entered = True
        while True:
            current = self.sessions.get(self.identity.login.id, timeout=switcher.remaining(deadline))
            if current is not None and current.active and current.uid == self.account.uid and current.vt == self.vt:
                return {"user": self.account.name, "vt": self.vt, "changed": True, "starting": True}
            self.sleep(0.05)

    def close(self):
        if self.closed:
            return
        self.guard()
        if not self.attempted:
            self.closed = True
            return
        if self.identity is None or self.pidfd is None:
            raise GraphicalRetained(self)
        rows = self.info()
        if rows["LoadState"] != "not-found" and (
                rows["Transient"] != "yes" or rows["InvocationID"] != self.identity.invocation
                or rows["MainPID"] not in {"0", str(self.pid)}):
            raise GraphicalRetained(self)
        owner = self.login()
        if owner is not None:
            scope = self.info(owner.scope)
            if (scope["LoadState"] != "loaded" or scope["Transient"] != "yes"
                    or scope["InvocationID"] != self.identity.scope_invocation or scope["ControlGroup"] != self.identity.cgroup):
                raise GraphicalRetained(self)
            if not self.process.exited(self.pidfd) and self.process_start(self.identity.cgroup) != self.identity.start:
                raise GraphicalRetained(self)
            self.sessions.command.run("terminate-session", owner.id, timeout=5)
            finish_abandoned_login(self.identity, self.sessions, self.command,
                                   exited=lambda: self.process.exited(self.pidfd))
            deadline = self.clock() + 8
            while self.login() is not None:
                if self.clock() >= deadline:
                    raise GraphicalPending("Waiting for the captured graphical login to close")
                self.sleep(0.05)
        scope = self.info(self.identity.login.scope)
        if scope["LoadState"] != "not-found" and (
                scope["InvocationID"] != self.identity.scope_invocation or scope["ControlGroup"]
                or scope["ActiveState"] not in {"inactive", "failed"}):
            raise GraphicalRetained(self)
        rows = self.info()
        if rows["LoadState"] != "not-found":
            if rows["Transient"] != "yes" or rows["InvocationID"] != self.identity.invocation:
                raise GraphicalRetained(self)
            self.command.run("/usr/bin/systemctl", "--job-mode=fail", "stop", self.name)
        if not self.process.exited(self.pidfd, 5):
            raise GraphicalRetained(self)
        rows = self.info()
        if rows["LoadState"] != "not-found" and (rows["MainPID"] != "0" or rows["ActiveState"] not in {"inactive", "failed"}):
            raise GraphicalRetained(self)
        self.process.close(self.pidfd)
        self.pidfd = None
        self.closed = True
