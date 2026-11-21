# SPDX-License-Identifier: GPL-3.0-or-later
"""Local account validation and bounded, non-PAM shared-password synchronization."""
from dataclasses import dataclass
import os
from pathlib import Path
import stat
import subprocess

from .model import account_name, label


class AccountOperationError(RuntimeError):
    """A failure message that omits stdin, hashes and captured tool diagnostics."""


def database_lines(path):
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError("Local account databases must be trusted root-owned regular files")
        if info.st_size > 4 * 1024 * 1024:
            raise ValueError("Local account database exceeds its limit")
        with os.fdopen(fd, "rb", closefd=False) as stream:
            consumed = 0
            while True:
                line = stream.readline(8193)
                if not line:
                    break
                consumed += len(line)
                if len(line) > 8192 or consumed > 4 * 1024 * 1024 or b"\0" in line:
                    raise ValueError("Invalid or oversized local account record")
                yield line.rstrip(b"\n").decode("utf-8").split(":")
    finally:
        os.close(fd)


@dataclass(frozen=True)
class LocalAccount:
    name: str
    uid: int
    gid: int
    home: str
    shell: str


class LocalAccounts:
    def __init__(self, passwd=Path("/etc/passwd"), shadow=Path("/etc/shadow")):
        self.passwd = Path(passwd)
        self.shadow = Path(shadow)

    def account(self, name, *, missing_ok=False):
        account_name(name)
        found, uid_counts = [], {}
        for fields in database_lines(self.passwd):
            if len(fields) == 7 and fields[2].isdecimal():
                uid = int(fields[2])
                uid_counts[uid] = uid_counts.get(uid, 0) + 1
            if fields[0] != name:
                continue
            if (len(fields) != 7 or not fields[2].isdecimal() or not fields[3].isdecimal()
                    or not 1000 <= int(fields[2]) < 65534 or not 0 <= int(fields[3]) < 2**32
                    or not Path(fields[5]).is_absolute()
                    or not Path(fields[6]).is_absolute()
                    or Path(fields[6]).name in {"nologin", "false"}):
                raise ValueError("Desktop owner and children must be ordinary local login accounts")
            found.append(LocalAccount(name, int(fields[2]), int(fields[3]), fields[5], fields[6]))
        if len(found) != 1:
            if missing_ok and not found:
                return None
            raise ValueError("Desktop account is missing or ambiguous in the local database")
        if uid_counts[found[0].uid] != 1:
            raise ValueError("A desktop account must have a unique local user ID")
        return found[0]

    def by_uid(self, uid):
        if type(uid) is not int or not 1000 <= uid < 65534:
            raise ValueError("Only ordinary local users can own desktops")
        names = [fields[0] for fields in database_lines(self.passwd)
                 if len(fields) == 7 and fields[2].isdecimal() and int(fields[2]) == uid]
        if len(names) != 1:
            raise ValueError("Desktop caller has no unique local account")
        return self.account(names[0])

    def caption(self, name):
        self.account(name)
        rows = [fields for fields in database_lines(self.passwd) if fields[0] == name]
        if len(rows) != 1 or len(rows[0]) != 7:
            raise ValueError("Desktop display name has no unique local account")
        value = rows[0][4].split(",", 1)[0].strip()
        return label(value) if value else name

    def shadow_record(self, name):
        account_name(name)
        found = []
        for fields in database_lines(self.shadow):
            if fields[0] == name:
                if len(fields) != 9 or "\r" in fields[1] or len(fields[1].encode("utf-8")) > 4096:
                    raise ValueError("Invalid local password record")
                found.append(fields)
        if len(found) != 1:
            raise ValueError("Desktop account has no unique local password record")
        return found[0]

    def password_hash(self, name):
        return self.shadow_record(name)[1]

    def expiry_day(self, name):
        value = self.shadow_record(name)[7]
        if value in {"", "-1"}:
            return None
        if not value.isdecimal():
            raise ValueError("Invalid local account expiry record")
        return int(value)


class RootCommands:
    def run(self, operation, *arguments, input=None):
        if os.geteuid() != 0:
            raise PermissionError("Only root can change local account credentials")
        binaries = {"chpasswd": "/usr/sbin/chpasswd", "useradd": "/usr/sbin/useradd",
                    "userdel": "/usr/sbin/userdel", "usermod": "/usr/sbin/usermod"}
        if operation not in binaries:
            raise ValueError("Unsupported credential operation")
        if any(not isinstance(arg, str) or "\0" in arg for arg in arguments):
            raise ValueError("Invalid credential operation arguments")
        if input is not None and (not isinstance(input, str) or len(input.encode("utf-8")) > 4 * 1024 * 1024):
            raise ValueError("Credential batch exceeds its limit")
        try:
            result = subprocess.run([binaries[operation], *arguments], input=input,
                text=True, encoding="utf-8", stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=15, check=False, close_fds=True,
                env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"})
        except (OSError, subprocess.SubprocessError):
            raise AccountOperationError("The local credential operation could not finish") from None
        if result.returncode:
            raise AccountOperationError("The local credential operation failed") from None
        return result


class PasswordSync:
    def __init__(self, store, accounts=None, commands=None):
        self.store = store
        self.accounts = accounts if accounts is not None else LocalAccounts()
        self.commands = commands if commands is not None else RootCommands()

    @staticmethod
    def system_context(environment):
        if os.geteuid() != 0 or "PKEXEC_UID" in environment:
            raise PermissionError("Only the system password stack or administrator synchronizes passwords")

    def _changes(self, family, source):
        # Validate every intended target before the one native batch is submitted.
        self.accounts.account(family[0])
        self.accounts.account(source)
        wanted = self.accounts.password_hash(source) or "!"
        changes = []
        for target in family:
            if target == source:
                continue
            entry = self.accounts.account(target, missing_ok=True)
            if entry is None:
                continue
            if target != family[0] and self.store.settings(target).password == "separate":
                continue
            if self.accounts.password_hash(target) != wanted:
                changes.append((target, wanted))
        return changes

    def _apply(self, changes):
        if changes:
            # --encrypted bypasses PAM, avoids recursive hooks, and lets the
            # native tool validate the entire batch before committing it.
            self.commands.run("chpasswd", "--encrypted",
                              input="".join(name + ":" + password + "\n" for name, password in changes))
        return len(changes)

    def all(self, *, environment=None):
        self.system_context(os.environ if environment is None else environment)
        with self.store.locked():
            mapping = self.store.ownership()
            changes = []
            for family in mapping.families:
                changes.extend(self._changes(family, family[0]))
            return self._apply(changes)

    def after_password_change(self, *, environment=None):
        environment = os.environ if environment is None else environment
        self.system_context(environment)
        if environment.get("PAM_TYPE") != "password":
            raise PermissionError("Password-change synchronization requires the PAM password stack")
        changed = account_name(environment.get("PAM_USER", ""))
        with self.store.locked():
            mapping = self.store.ownership()
            family = mapping.family(changed)
            if len(family) == 1:
                return 0
            if mapping.is_child(changed) and self.store.settings(changed).password == "separate":
                return 0
            return self._apply(self._changes(family, changed))
