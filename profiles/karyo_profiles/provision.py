# SPDX-License-Identifier: GPL-3.0-or-later
"""Create only a new local child, enabling its password after trusted metadata."""
from dataclasses import dataclass
import os
from pathlib import Path
import re
import stat

from .credentials import LocalAccounts, RootCommands, database_lines
from .model import DesktopAddress, DesktopSettings, account_name, label
from .storage import trusted_directory

SHARED_GROUPS = ("audio", "video", "plugdev", "netdev", "bluetooth", "scanner", "lpadmin", "docker", "sudo")


class CreationError(RuntimeError):
    def __init__(self, name, retained=False):
        self.name = name
        self.retained = retained
        super().__init__(f"Desktop {name} needs administrator review; account data was retained."
                         if retained else f"Could not create desktop {name}; its pending account was rolled back.")


@dataclass(frozen=True)
class PendingAccount:
    account: object
    home_device: int
    home_inode: int


class LocalBackend:
    def __init__(self, accounts=None, commands=None, groups=Path("/etc/group"), homes=Path("/home")):
        self.accounts = accounts if accounts is not None else LocalAccounts()
        self.commands = commands if commands is not None else RootCommands()
        self.groups = Path(groups)
        self.homes = Path(homes)

    def names(self):
        return {row[0] for path in (self.accounts.passwd, self.groups) for row in database_lines(path)}

    def home_exists(self, name):
        directory = trusted_directory(self.homes)
        try:
            try:
                os.stat(account_name(name), dir_fd=directory, follow_symlinks=False)
                return True
            except FileNotFoundError:
                return False
        finally:
            os.close(directory)

    def inherited_groups(self, owner):
        groups = []
        for row in database_lines(self.groups):
            if row[0] not in SHARED_GROUPS:
                continue
            if len(row) != 4 or not row[2].isdecimal():
                raise ValueError("Invalid local inherited group")
            if int(row[2]) == owner.gid or owner.name in row[3].split(","):
                groups.append(row[0])
        if len(set(groups)) != len(groups):
            raise ValueError("Ambiguous inherited group names")
        return tuple(groups)

    def create(self, name, caption, groups):
        caption = label(caption)
        if self.home_exists(name) or name in self.names():
            raise ValueError("Existing account, group or home cannot become a new desktop")
        if any(group not in SHARED_GROUPS for group in groups):
            raise ValueError("Unexpected inherited desktop group")
        home = self.homes / name
        arguments = ["--create-home", "--user-group", "--home-dir", str(home), "--shell", "/bin/bash",
                     "--password", "!", "--expiredate", "1970-01-02", "--comment", caption]
        # Explicit empty membership also overrides administrator useradd defaults.
        arguments.extend(("--groups", ",".join(groups)))
        self.commands.run("useradd", *arguments, "--", name)
        account = self.accounts.account(name)
        info = home.lstat()
        initial = self.accounts.password_hash(name)
        if (account.home != str(home) or not stat.S_ISDIR(info.st_mode) or info.st_uid != account.uid
                or not initial.startswith(("!", "*")) or self.accounts.expiry_day(name) != 1):
            raise RuntimeError("The new account is not a proven locked, newly owned home")
        return PendingAccount(account, info.st_dev, info.st_ino)

    @staticmethod
    def has_processes(uid):
        for path in Path("/proc").iterdir():
            if not path.name.isdecimal():
                continue
            try:
                if path.stat().st_uid == uid:
                    return True
            except FileNotFoundError:
                pass
        return False

    def can_rollback(self, pending):
        account = pending.account
        if self.accounts.account(account.name, missing_ok=True) != account:
            return False
        if self.accounts.expiry_day(account.name) != 1 or self.has_processes(account.uid):
            return False
        home = Path(account.home)
        if home != self.homes / account.name:
            return False
        info = home.lstat()
        return (stat.S_ISDIR(info.st_mode) and info.st_uid == account.uid
                and (info.st_dev, info.st_ino) == (pending.home_device, pending.home_inode))

    def remove_pending(self, pending):
        if not self.can_rollback(pending):
            raise RuntimeError("New account no longer matches safe rollback conditions")
        # Never force removal; userdel also refuses a user with running processes.
        self.commands.run("userdel", "--remove", "--", pending.account.name)

    def activate(self, pending, settings, password, owner_hash):
        name = pending.account.name
        if settings.password == "separate":
            self.commands.run("chpasswd", "--crypt-method", "YESCRYPT", input=name + ":" + password + "\n")
        else:
            self.commands.run("chpasswd", "--encrypted", input=name + ":" + (owner_hash or "!") + "\n")
        # Account expiry blocks password and key-based logins throughout preparation.
        # Clearing that staging marker is the final activation, after all metadata.
        self.commands.run("usermod", "--expiredate", "", "--", name)


class AccountProvisioner:
    def __init__(self, store, backend=None):
        self.store = store
        self.backend = backend if backend is not None else LocalBackend()

    def choose_name(self, caption, explicit, mapping):
        taken = self.backend.names() | {name for family in mapping.families for name in family}
        if explicit:
            if re.fullmatch(r"[a-z][a-z0-9-]{0,30}", explicit) is None:
                raise ValueError("New desktop login must start with a lower-case letter and use letters, digits or dashes")
            candidates = [explicit]
        else:
            stem = re.sub(r"[^a-z0-9]+", "-", caption.lower()).strip("-")[:31].rstrip("-")
            if not stem or not stem[0].isalpha():
                stem = "desktop"
            candidates = [stem, *(stem[:26].rstrip("-") + "-" + str(n) for n in range(2, 1002))]
        for name in candidates:
            if name not in taken and not self.backend.home_exists(name) and not self.store.has_settings(name):
                return name
        raise ValueError("The desktop name, group, metadata or home directory is already in use")

    def add(self, caller_uid, caption, *, name="", protection="none", password=None):
        if os.geteuid() != 0:
            raise PermissionError("Only the privileged desktop helper creates users")
        caption = label(caption)
        if not isinstance(name, str):
            raise ValueError("Desktop login must be text")
        settings = DesktopSettings(password=protection)
        if settings.password == "separate":
            if (not isinstance(password, str) or not 1 <= len(password.encode("utf-8")) <= 1024
                    or any(c in password for c in "\0\r\n")):
                raise ValueError("A separate password must be one nonempty bounded line")
        elif password is not None:
            raise ValueError("A shared desktop must not receive a separate password")
        with self.store.locked():
            caller = self.backend.accounts.by_uid(caller_uid)
            before = self.store.ownership()
            owner = self.backend.accounts.account(before.owner(caller.name))
            child = self.choose_name(caption, name, before)
            after = before.with_child(caller.name, child)
            # Read/validate everything possible before creating a locked account.
            owner_hash = self.backend.accounts.password_hash(owner.name)
            groups = self.backend.inherited_groups(owner)
            pending = None
            try:
                pending = self.backend.create(child, caption, groups)
                DesktopAddress.for_uid(pending.account.uid)
                if self.backend.has_processes(pending.account.uid):
                    raise RuntimeError("The allocated user ID already has processes")
                self.store.save_settings(child, settings)
                self.store.save_ownership(after)
                # No fallible publication follows this activation. Future session
                # startup must hold the same store lock while reading eligibility.
                self.backend.activate(pending, settings, password, owner_hash)
                return pending.account
            except Exception:
                if pending is None:
                    # A failed useradd can have partial effects. Never delete an
                    # account whose complete new identity we did not capture.
                    try:
                        retained = self.backend.accounts.account(child, missing_ok=True) is not None
                    except Exception:
                        retained = True
                    raise CreationError(child, retained=retained) from None
                try:
                    if not self.backend.can_rollback(pending):
                        raise RuntimeError("New account may already be usable or changed")
                    current = self.store.ownership()
                    if current == after:
                        self.store.save_ownership(before)
                    elif current != before:
                        raise RuntimeError("Ownership changed outside the pending operation")
                    if self.store.has_settings(child):
                        self.store.remove_new_settings(child, settings)
                    self.backend.remove_pending(pending)
                except Exception:
                    raise CreationError(child, retained=True) from None
                raise CreationError(child) from None
