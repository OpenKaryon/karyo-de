# SPDX-License-Identifier: GPL-3.0-or-later
"""Persistent root-owned service state tied to an actual account/home generation."""
from dataclasses import asdict
import json
import os
from pathlib import Path
import stat

from .model import root_file
from .services import ServiceError, host_controller
from .storage import atomic_write, trusted_directory

STATE_ROOT = Path("/var/lib/karyo/profile-vpn")


class ProviderState:
    def __init__(self, files, kind, root=STATE_ROOT):
        if kind not in {"netbird", "network-manager"}:
            raise ValueError("Unsupported managed network service state")
        self.files = files
        self.account = files.accounts.by_uid(files.network.spec.uid)
        self.path = root / str(self.account.uid) / kind
        self.owner = None
        self.home_identity = None
        self.fd = None
        self.identity = None

    def capture_home(self):
        path = Path(self.account.home)
        parent = trusted_directory(path.parent)
        try:
            fd = os.open(path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
        finally:
            os.close(parent)
        try:
            info = os.fstat(fd)
            if info.st_uid != self.account.uid or info.st_mode & 0o002:
                raise ServiceError("The provider account home is not a private account-owned directory")
            return info.st_dev, info.st_ino
        finally:
            os.close(fd)

    def guard(self):
        host_controller()
        self.files.verify()
        if self.files.accounts.by_uid(self.account.uid) != self.account:
            raise ServiceError("The provider state account changed")
        if self.home_identity is not None and self.capture_home() != self.home_identity:
            raise ServiceError("The provider account home generation changed")

    def owner_contents(self, home):
        return json.dumps({"version": 1, "account": asdict(self.account), "home": list(home)},
                          sort_keys=True, separators=(",", ":")) + "\n"

    def prepare(self):
        self.guard()
        if self.fd is not None:
            raise ServiceError("Provider state was already captured")
        self.home_identity = self.capture_home()
        self.owner = self.owner_contents(self.home_identity)
        parent = trusted_directory(self.path.parent, create=True)
        created = False
        try:
            try:
                os.mkdir(self.path.name, 0o700, dir_fd=parent)
                created = True
                os.fsync(parent)
            except FileExistsError:
                pass
            self.fd = os.open(self.path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                              dir_fd=parent)
        finally:
            os.close(parent)
        try:
            info = os.fstat(self.fd)
            if info.st_uid != 0 or info.st_gid != 0 or stat.S_IMODE(info.st_mode) != 0o700:
                raise ServiceError("Provider state must be a private root-owned directory")
            self.identity = info.st_dev, info.st_ino
            if created:
                if os.listdir(self.fd):
                    raise ServiceError("Fresh provider state unexpectedly contains files")
                atomic_write(self.fd, "owner.json", self.owner.encode("ascii"), mode=0o600)
            self.verify()
            return self
        except BaseException:
            self.release_handles()
            # Unknown state or a lost publication ACK is retained, never adopted
            # by recursively chowning/removing a directory on the next attempt.
            raise

    def verify(self):
        self.guard()
        if self.fd is None or self.identity is None:
            raise ServiceError("The provider state was not captured")
        current = trusted_directory(self.path)
        try:
            info = os.fstat(current)
            if ((info.st_dev, info.st_ino) != self.identity or info.st_gid != 0
                    or stat.S_IMODE(info.st_mode) != 0o700):
                raise ServiceError("The provider state directory changed")
            owner = os.stat("owner.json", dir_fd=current, follow_symlinks=False)
            if owner.st_nlink != 1 or stat.S_IMODE(owner.st_mode) != 0o600:
                raise ServiceError("Unsafe provider ownership stamp")
            if root_file("owner.json", dir_fd=current) != self.owner:
                raise ServiceError("Provider state belongs to another account generation")
        finally:
            os.close(current)

    def release_handles(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
