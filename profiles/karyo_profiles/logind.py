# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded logind inventory and privileged caller validation for desktop helpers."""
from dataclasses import dataclass
import ctypes
import json
import os
import re
import selectors
import subprocess
import time

from .credentials import LocalAccounts
from .model import account_name

SESSION_ID = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}")
PROPERTIES = ("Id", "Name", "User", "Type", "Seat", "VTNr", "Active", "State", "Class", "Remote", "LockedHint")


class SessionError(RuntimeError):
    pass


def session_id(value):
    if not isinstance(value, str) or SESSION_ID.fullmatch(value) is None:
        raise ValueError("Invalid logind session identifier")
    return value


def process_session(pid=0):
    # The process's kernel cgroup membership is authoritative; environment
    # variables such as XDG_SESSION_ID and USER are deliberately ignored.
    if type(pid) is not int or not 0 <= pid < 2**31:
        raise ValueError("Invalid native process identifier")
    library = ctypes.CDLL("libsystemd.so.0")
    library.sd_pid_get_session.argtypes = (ctypes.c_int, ctypes.POINTER(ctypes.c_void_p))
    library.sd_pid_get_session.restype = ctypes.c_int
    libc = ctypes.CDLL(None)
    libc.free.argtypes = (ctypes.c_void_p,)
    libc.free.restype = None
    value = ctypes.c_void_p()
    result = library.sd_pid_get_session(pid, ctypes.byref(value))
    if result < 0:
        raise PermissionError("The desktop helper is not in a local graphical login session")
    try:
        return session_id(ctypes.string_at(value).decode("ascii"))
    finally:
        libc.free(value)


class LoginCommand:
    def run(self, *arguments, timeout):
        if not 0 < timeout <= 10:
            raise TimeoutError("Desktop session lookup deadline expired")
        command = ["/usr/bin/loginctl", "--no-pager", *arguments]
        try:
            with subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL, close_fds=True,
                    env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"}) as process:
                try:
                    data = bytearray()
                    deadline = time.monotonic() + timeout
                    with selectors.DefaultSelector() as ready:
                        ready.register(process.stdout, selectors.EVENT_READ)
                        while True:
                            remaining = deadline - time.monotonic()
                            if remaining <= 0 or not ready.select(remaining):
                                raise TimeoutError("Desktop session lookup deadline expired")
                            block = os.read(process.stdout.fileno(), 8192)
                            if not block:
                                break
                            data.extend(block)
                            if len(data) > 256 * 1024:
                                raise SessionError("Desktop session response exceeds its limit")
                    process.wait(timeout=max(0.001, deadline - time.monotonic()))
                    if process.returncode:
                        raise SessionError("The login manager could not read desktop sessions")
                    return data.decode("utf-8")
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
        except TimeoutError:
            raise
        except (OSError, subprocess.SubprocessError, UnicodeError):
            raise SessionError("The login manager could not read desktop sessions") from None


@dataclass(frozen=True)
class Session:
    id: str
    name: str
    uid: int
    vt: int
    active: bool
    locked: bool = False

    @classmethod
    def parse(cls, text, expected):
        values = {}
        for line in text.splitlines():
            key, separator, value = line.partition("=")
            if not separator or key not in PROPERTIES or key in values:
                raise ValueError("Invalid or ambiguous login manager properties")
            values[key] = value
        if values.get("Id") != session_id(expected):
            raise ValueError("Login manager returned another session")
        if (values.get("Type") not in {"wayland", "x11"} or values.get("Seat") != "seat0"
                or values.get("Class") != "user" or values.get("Remote") != "no"
                or values.get("State") not in {"active", "online"}):
            return None
        uid, vt = values.get("User", ""), values.get("VTNr", "")
        if (not uid.isascii() or not uid.isdecimal() or not 1000 <= int(uid) < 65534
                or not vt.isascii() or not vt.isdecimal() or not 1 <= int(vt) <= 63
                or values.get("Active") not in {"yes", "no"}
                or values.get("LockedHint") not in {"yes", "no"}):
            raise ValueError("Invalid desktop session identity or virtual terminal")
        return cls(expected, account_name(values.get("Name", "")), int(uid), int(vt),
                   values["Active"] == "yes", values["LockedHint"] == "yes")


@dataclass(frozen=True)
class Greeter:
    id: str
    name: str
    uid: int
    vt: int
    active: bool

    @classmethod
    def parse(cls,text,expected):
        values={}
        for line in text.splitlines():
            key,separator,value=line.partition("=")
            if not separator or key not in PROPERTIES or key in values:
                raise ValueError("Invalid greeter session properties")
            values[key]=value
        if values.get("Id")!=session_id(expected):raise ValueError("Another greeter session was returned")
        if (values.get("Type") not in {"wayland","x11"} or values.get("Seat")!="seat0"
                or values.get("Class")!="greeter" or values.get("Remote")!="no"
                or values.get("State") not in {"active","online"}):return None
        uid,vt=values.get("User",""),values.get("VTNr","")
        if (set(values)!=set(PROPERTIES) or not uid.isascii() or not uid.isdecimal()
                or str(int(uid))!=uid or not 1<=int(uid)<1000 or not vt.isascii()
                or not vt.isdecimal() or str(int(vt))!=vt or not 1<=int(vt)<=63
                or values["Active"] not in {"yes","no"} or values["LockedHint"] not in {"yes","no"}):
            raise ValueError("Invalid local greeter identity or console")
        return cls(expected,account_name(values["Name"]),int(uid),int(vt),values["Active"]=="yes")


OWNER_PROPERTIES = ("Id", "Name", "User", "Leader", "Class", "Scope", "Service", "State")


@dataclass(frozen=True)
class LoginOwner:
    id: str
    name: str
    uid: int
    leader: int
    kind: str
    scope: str
    service: str
    state: str

    @property
    def identity(self):
        return (self.id, self.name, self.uid, self.leader, self.kind, self.scope, self.service)

    @classmethod
    def parse(cls, text, expected):
        rows = {}
        for line in text.splitlines():
            key, separator, value = line.partition("=")
            if not separator or key not in OWNER_PROPERTIES or key in rows:
                raise ValueError("Ambiguous logind process ownership")
            rows[key] = value
        if set(rows) != set(OWNER_PROPERTIES) or rows["Id"] != session_id(expected):
            raise ValueError("Incomplete logind process ownership")
        for key in ("User", "Leader"):
            value = rows[key]
            if not value.isascii() or not value.isdecimal() or str(int(value)) != value:
                raise ValueError("Invalid logind process owner identifier")
        if int(rows["User"]) >= 2**32 or int(rows["Leader"]) >= 2**31:
            raise ValueError("Logind process owner identifier is out of range")
        if (re.fullmatch("[a-z][a-z-]{0,31}", rows["Class"]) is None
                or re.fullmatch("[A-Za-z0-9_.@+-]{0,128}", rows["Service"]) is None
                or rows["Scope"] not in {"", "session-" + expected + ".scope"}
                or rows["State"] not in {"opening", "active", "online", "closing"}):
            raise ValueError("Invalid logind owner class, scope or state")
        return cls(expected, account_name(rows["Name"]), int(rows["User"]), int(rows["Leader"]),
                   rows["Class"], rows["Scope"], rows["Service"], rows["State"])


class Sessions:
    def __init__(self, command=None, clock=time.monotonic):
        self.command = command if command is not None else LoginCommand()
        self.clock = clock

    def get(self, identifier, *, timeout=5):
        session_id(identifier)
        text = self.command.run("show-session", identifier, "--all",
                                *("--property=" + name for name in PROPERTIES), timeout=timeout)
        return Session.parse(text, identifier)

    def lock(self, identifier, *, timeout=2):
        self.command.run("lock-session", session_id(identifier), timeout=timeout)

    def activate(self, identifier, *, timeout=2):
        self.command.run("activate", session_id(identifier), timeout=timeout)

    def _identifiers(self, *, timeout=5):
        text = self.command.run("list-sessions", "--json=short", timeout=timeout)
        rows = json.loads(text)
        if not isinstance(rows, list) or len(rows) > 256:
            raise ValueError("Unexpected login manager session inventory")
        identifiers = []
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError("Unexpected login manager session row")
            identifier = session_id(row.get("session"))
            if identifier in identifiers:
                raise ValueError("Duplicate login manager session identifier")
            identifiers.append(identifier)
        return tuple(identifiers)

    def seat_active(self,*,timeout=5):
        text=self.command.run("show-seat","seat0","--all","--property=Id","--property=ActiveSession",timeout=timeout)
        rows={}
        for line in text.splitlines():
            key,separator,value=line.partition("=")
            if not separator or key not in {"Id","ActiveSession"} or key in rows:
                raise ValueError("Invalid active seat properties")
            rows[key]=value
        if set(rows)!={"Id","ActiveSession"} or rows["Id"]!="seat0":
            raise ValueError("Another or incomplete native seat was returned")
        return session_id(rows["ActiveSession"]) if rows["ActiveSession"] else ""

    def owner(self,identifier,*,timeout=5):
        text=self.command.run("show-session",session_id(identifier),"--all",
            *("--property="+name for name in OWNER_PROPERTIES),timeout=timeout)
        return LoginOwner.parse(text,identifier)

    def greeter(self,identifier,*,timeout=5):
        text=self.command.run("show-session",session_id(identifier),"--all",
            *("--property="+name for name in PROPERTIES),timeout=timeout)
        return Greeter.parse(text,identifier)

    def _inventory(self, reader):
        """Retry a complete inventory within one deadline; never skip unreadable logins."""
        deadline = self.clock() + 5
        for attempt in range(3):
            try:
                found = []
                for identifier in self._identifiers(timeout=deadline - self.clock()):
                    value = reader(identifier, timeout=deadline - self.clock())
                    if value is not None:
                        found.append(value)
                return tuple(found)
            except SessionError:
                # A listed login may end before show-session. Discard partial
                # results and read a fresh complete inventory to establish absence.
                # Persistent service errors still fail closed, never mean offline.
                if attempt == 2 or self.clock() >= deadline:
                    raise

    def greeters(self):
        return list(self._inventory(self.greeter))

    def _console_vt(self, identifier, *, timeout):
        text = self.command.run("show-session", identifier, "--all", "--property=Id", "--property=VTNr",
                                timeout=timeout)
        values = {}
        for line in text.splitlines():
            key, separator, value = line.partition("=")
            if not separator or key not in {"Id", "VTNr"} or key in values:
                raise ValueError("Invalid console ownership properties")
            values[key] = value
        if set(values) != {"Id", "VTNr"} or values["Id"] != identifier:
            raise ValueError("Console ownership returned another or incomplete session")
        vt = values["VTNr"]
        if not vt.isascii() or not vt.isdecimal() or str(int(vt)) != vt or not 0 <= int(vt) <= 63:
            raise ValueError("Invalid console owner virtual terminal")
        return int(vt)

    def occupied_vts(self):
        # Console ownership is global, not limited to selectable graphical user
        # sessions: greeters, TTY users and closing sessions must reserve theirs.
        return frozenset(vt for vt in self._inventory(self._console_vt) if vt)

    def owners(self, uid):
        """Read a complete UID inventory; retry churn, never infer absence from errors."""
        if type(uid) is not int or not 1000 <= uid < 65534:
            raise ValueError("Invalid desktop owner UID")
        return tuple(owner for owner in self._inventory(self.owner) if owner.uid == uid)

    def all(self):
        return self._inventory(self.get)


def authorized_caller(environment, *, accounts=None, sessions=None, identify=process_session):
    if os.getuid() != 0 or os.geteuid() != 0:
        raise PermissionError("Run the desktop operation through its polkit helper")
    value = environment.get("PKEXEC_UID", "")
    if (not isinstance(value, str) or not value.isascii() or not value.isdecimal()
            or str(int(value)) != value):
        raise PermissionError("The desktop operation has no authenticated polkit caller")
    accounts = accounts if accounts is not None else LocalAccounts()
    sessions = sessions if sessions is not None else Sessions()
    caller = accounts.by_uid(int(value))
    current = sessions.get(identify())
    if current is None or not current.active or current.uid != caller.uid or current.name != caller.name:
        raise PermissionError("Only the active local graphical desktop may perform this operation")
    return caller


def listing(caller, store, accounts, sessions):
    names = store.ownership().family(caller.name)
    running = sessions.all()
    home = names[0] if any(store.settings(name).workstation for name in names[1:]) else None
    result = []
    for name in names:
        account = accounts.account(name, missing_ok=True)
        if account is None:
            if name == caller.name:
                raise ValueError("The current desktop account disappeared")
            continue
        candidates = [value for value in running if value.name == name and value.uid == account.uid]
        if len(candidates) > 1:
            # Ambiguous inactive sessions must not select an arbitrary switch target.
            active = [value for value in candidates if value.active]
            if len(active) != 1:
                raise ValueError("A desktop has multiple ambiguous graphical sessions")
            current = active[0]
        else:
            current = candidates[0] if candidates else None
        caption = "Home" if name == home else accounts.caption(name)
        result.append({"user": name, "label": caption, "running": current is not None,
                       "active": bool(current and current.active), "vt": current.vt if current else None,
                       "current": name == caller.name})
    return result
