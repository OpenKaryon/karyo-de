# SPDX-License-Identifier: GPL-3.0-or-later
"""Root-owned profile configuration with serialized, atomic single-file writes."""
from contextlib import contextmanager
import fcntl
import math
import os
from pathlib import Path
import stat
import time
import uuid

from .model import DesktopSettings, Ownership, account_name, root_file


def trusted_directory(path, *, create=False):
    """Open every directory component without following symlinks."""
    path = Path(path)
    if not path.is_absolute() or ".." in path.parts:
        raise ValueError("Configuration directories must be absolute without parent traversal")
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
    fd = os.open("/", flags)
    try:
        info = os.fstat(fd)
        if info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError("Unsafe filesystem root")
        for name in path.parts[1:]:
            try:
                child = os.open(name, flags, dir_fd=fd)
            except FileNotFoundError:
                if not create:
                    raise
                created = False
                try:
                    os.mkdir(name, 0o755, dir_fd=fd)
                    created = True
                except FileExistsError:
                    pass
                child = os.open(name, flags, dir_fd=fd)
                if created:
                    os.fchmod(child, 0o755)  # Public metadata stays readable despite a caller's umask.
                    os.fsync(fd)
            info = os.fstat(child)
            if info.st_uid != 0 or info.st_mode & 0o022:
                os.close(child)
                raise ValueError("Profile parent directories must be root-owned and not writable by others")
            os.close(fd)
            fd = child
        return fd
    except BaseException:
        os.close(fd)
        raise


def atomic_write(directory, name, data):
    """Replace a trusted regular destination only after complete, synced output."""
    if os.geteuid() != 0:
        raise PermissionError("Only root can write desktop configuration")
    # All names are fixed metadata names or validated local account basenames.
    if Path(name).name != name or name in {".", ".."} or "/" in name:
        raise ValueError("Configuration destination must be a basename")
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=directory)
    except FileNotFoundError:
        pass
    else:
        try:
            info = os.fstat(fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0
                    or info.st_mode & 0o022 or info.st_nlink != 1):
                raise ValueError("Existing profile configuration is not a trusted regular file")
        finally:
            os.close(fd)
    temporary = ".karyo-" + uuid.uuid4().hex + ".tmp"
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                 0o600, dir_fd=directory)
    renamed = False
    try:
        remaining = memoryview(data)
        while remaining:
            written = os.write(fd, remaining)
            if written <= 0:
                raise OSError("Incomplete profile configuration write")
            remaining = remaining[written:]
        os.fchmod(fd, 0o644)
        os.fsync(fd)
        os.replace(temporary, name, src_dir_fd=directory, dst_dir_fd=directory)
        renamed = True
        os.fsync(directory)
    finally:
        os.close(fd)
        if not renamed:
            os.unlink(temporary, dir_fd=directory)


class ConfigStore:
    def __init__(self, configuration=Path("/etc/karyo"), legacy=Path("/etc/openkaryon"),
                 runtime=Path("/run/karyo")):
        self.configuration = Path(configuration)
        self.legacy = Path(legacy)
        self.runtime = Path(runtime)
        self._locked = False

    @contextmanager
    def locked(self, *, timeout=10):
        if os.geteuid() != 0:
            raise PermissionError("Only root can change desktop configuration")
        if type(timeout) not in {int, float} or not math.isfinite(timeout) or not 0 <= timeout <= 30:
            raise ValueError("Profile lock timeout must be between zero and 30 seconds")
        if self._locked:
            raise RuntimeError("Nested profile configuration locks are not supported")
        directory = trusted_directory(self.runtime, create=True)
        lock = None
        try:
            lock = os.open("desktops.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC,
                           0o600, dir_fd=directory)
            info = os.fstat(lock)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0
                    or info.st_mode & 0o077 or info.st_nlink != 1):
                raise ValueError("Unsafe desktop configuration lock")
            deadline = time.monotonic() + timeout
            while True:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError("Another desktop configuration operation is still running") from None
                    time.sleep(min(0.05, remaining))
            self._locked = True
            yield self
        finally:
            self._locked = False
            if lock is not None:
                os.close(lock)
            os.close(directory)

    def _read(self, relative, *, missing=""):
        for root in (self.configuration, self.legacy):
            directory = None
            try:
                directory = trusted_directory(root / relative.parent)
                return root_file(relative.name, dir_fd=directory)
            except FileNotFoundError:
                continue
            finally:
                if directory is not None:
                    os.close(directory)
        return missing

    def ownership(self):
        return Ownership.parse(self._read(Path("desktops.conf")))

    def settings(self, name):
        return DesktopSettings.parse(self._read(Path("desktops.d") / (account_name(name) + ".conf")))

    def has_settings(self, name):
        return self._read(Path("desktops.d") / (account_name(name) + ".conf"), missing=None) is not None

    def remove_new_settings(self, name, expected):
        """Rollback only exactly the fresh canonical metadata written by this operation."""
        if not self._locked or os.geteuid() != 0:
            raise PermissionError("Profile rollback requires the root configuration lock")
        directory = trusted_directory(self.configuration / "desktops.d")
        try:
            filename = account_name(name) + ".conf"
            fd = os.open(filename, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                         dir_fd=directory)
            try:
                info = os.fstat(fd)
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0
                        or info.st_mode & 0o022 or info.st_nlink != 1):
                    raise ValueError("Fresh profile metadata is no longer trusted")
            finally:
                os.close(fd)
            if root_file(filename, dir_fd=directory) != expected.serialize():
                raise ValueError("Fresh profile metadata changed during account provisioning")
            os.unlink(filename, dir_fd=directory)
            os.fsync(directory)
        finally:
            os.close(directory)

    def _save(self, relative, content):
        if not self._locked or os.geteuid() != 0:
            raise PermissionError("Profile writes require the root configuration lock")
        directory = trusted_directory(self.configuration / relative.parent, create=True)
        try:
            atomic_write(directory, relative.name, content.encode("utf-8"))
        finally:
            os.close(directory)

    def save_ownership(self, ownership):
        # Revalidate serialized content before it becomes authoritative system data.
        content = ownership.serialize()
        Ownership.parse(content)
        self._save(Path("desktops.conf"), content)

    def save_settings(self, name, settings):
        content = settings.serialize()
        self._save(Path("desktops.d") / (account_name(name) + ".conf"), content)
