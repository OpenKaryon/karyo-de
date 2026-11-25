# SPDX-License-Identifier: GPL-3.0-or-later
"""Captured per-lifetime worker paths and immutable prepared publication."""
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import stat

from .services import host_controller
from .storage import atomic_write, trusted_directory

RUNTIME = Path('/run/karyo/desktop-workers')
STATE = Path('/var/lib/karyo/desktop-workers')


class WorkerFiles:
    def __init__(self, context, instance):
        if not isinstance(instance, str) or re.fullmatch('[a-f0-9]{32}', instance) is None:
            raise ValueError('Invalid worker instance identity')
        context.verify()
        self.context, self.instance = context, instance
        self.runtime = RUNTIME/'instances'/instance
        self.state = STATE/context.desktop_token/instance
        self.prepared = RUNTIME/'prepared'/(instance+'.json')
        self.control = RUNTIME/'control'/instance
        self.identities = {}
        self.prepared_digest = self.prepared_identity = self.listener_identity = None
        self.listener = None

    def guard(self):
        host_controller()
        if not self.context.scope.network.network.store._locked:
            raise PermissionError('Worker files require their profile owner lock')

    def create_directory(self, path, mode, gid):
        parent = trusted_directory(path.parent, create=True)
        try:
            os.mkdir(path.name, mode, dir_fd=parent)
            fd = os.open(path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                         dir_fd=parent)
            try:
                os.fchown(fd, 0, gid)
                os.fchmod(fd, mode)
                info = os.fstat(fd)
                self.identities[path] = (info.st_dev, info.st_ino, mode, gid)
                os.fsync(parent)
            finally:
                os.close(fd)
        finally:
            os.close(parent)

    def prepare(self):
        self.guard()
        if self.identities:
            raise RuntimeError('Worker instance files were already prepared')
        # The journal intent must exist before calling this function. Unknown
        # partial objects stay retained; an exception never recursively removes
        # a directory which was not captured by this lifetime.
        self.create_directory(self.state, 0o700, 0)
        self.create_directory(self.runtime, 0o750, self.context.origin.identity.gid)
        for path in (self.prepared.parent, self.control.parent):
            fd = trusted_directory(path, create=True)
            try:
                os.fchmod(fd, 0o700)
            finally:
                os.close(fd)
        if os.path.lexists(self.prepared) or os.path.lexists(self.control):
            raise RuntimeError('An existing worker publication is retained')
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_CLOEXEC)
        self.listener.bind(str(self.control))
        os.chmod(self.control, 0o600)
        self.listener.listen(1)
        info = self.control.stat()
        self.listener_identity = info.st_dev, info.st_ino
        return self

    def publish_prepared(self, value):
        self.verify()
        if self.prepared_digest is not None:
            raise RuntimeError('A prepared worker record is immutable')
        if (not isinstance(value, dict) or value.get('version') != 1
                or value.get('role') != 'managed-worker-bootstrap'
                or value.get('worker', {}).get('id') != self.instance):
            raise ValueError('Prepared publication must describe this worker')
        data = (json.dumps(value, ensure_ascii=True, allow_nan=False, separators=(',', ':'),
                           sort_keys=True)+'\n').encode('ascii')
        if len(data) > 65536:
            raise ValueError('Prepared worker record exceeds its bound')
        parent = trusted_directory(self.prepared.parent)
        try:
            if os.path.lexists(self.prepared):
                raise RuntimeError('An existing prepared worker record is retained')
            atomic_write(parent, self.prepared.name, data, mode=0o400)
            info = self.prepared.stat()
            self.prepared_identity = info.st_dev, info.st_ino
            self.prepared_digest = hashlib.sha256(data).hexdigest()
        finally:
            os.close(parent)
        self.verify()
        return self.prepared_digest

    def verify(self):
        self.guard()
        if len(self.identities) != 2 or self.listener_identity is None:
            raise RuntimeError('The worker instance has no captured file lifetime')
        for path, expected in self.identities.items():
            fd = trusted_directory(path)
            try:
                info = os.fstat(fd)
                actual = info.st_dev, info.st_ino, stat.S_IMODE(info.st_mode), info.st_gid
                if actual != expected:
                    raise RuntimeError('A captured worker instance directory changed')
            finally:
                os.close(fd)
        info = self.control.lstat()
        if (not stat.S_ISSOCK(info.st_mode) or info.st_uid != 0 or info.st_gid != 0
                or stat.S_IMODE(info.st_mode) != 0o600
                or (info.st_dev, info.st_ino) != self.listener_identity):
            raise RuntimeError('The worker owner control endpoint changed')
        if self.prepared_digest is not None:
            fd = os.open(self.prepared, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
            try:
                info = os.fstat(fd)
                data = os.read(fd, 65537)
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_gid != 0
                        or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o400
                        or (info.st_dev, info.st_ino) != self.prepared_identity
                        or hashlib.sha256(data).hexdigest() != self.prepared_digest):
                    raise RuntimeError('The immutable worker prepared record changed')
            finally:
                os.close(fd)

    def close_handles(self):
        if self.listener is not None:
            self.listener.close()
            self.listener = None
