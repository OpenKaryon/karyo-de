# SPDX-License-Identifier: GPL-3.0-or-later
"""Captured per-lifetime worker paths and immutable prepared publication."""
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import stat
import shutil
from types import SimpleNamespace

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
        self.recovery_from = None
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
        if self.listener_identity is not None or self.runtime in self.identities:
            raise RuntimeError('Worker instance files were already prepared')
        # The journal intent must exist before calling this function. Unknown
        # partial objects stay retained; an exception never recursively removes
        # a directory which was not captured by this lifetime.
        if self.recovery_from is None:
            self.create_directory(self.state, 0o700, 0)
        self.create_directory(self.runtime, 0o750, self.context.scope.manager.account.gid)
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
        if (not isinstance(value, dict) or type(value.get('version')) is not int or value['version'] != 1
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

    def receipt(self):
        self.verify()
        if self.prepared_digest is None:
            raise RuntimeError('The worker has no immutable prepared record')
        return {'state': list(self.identities[self.state]), 'runtime': list(self.identities[self.runtime]),
                'control': list(self.listener_identity), 'prepared': list(self.prepared_identity),
                'sha256': self.prepared_digest}

    @classmethod
    def restore(cls, scope, instance, receipt, *, state_instance=None, partial=False):
        state_instance = instance if state_instance is None else state_instance
        for value in (instance, state_instance):
            if not isinstance(value, str) or re.fullmatch('[a-f0-9]{32}', value) is None:
                raise ValueError('Invalid captured worker instance')
        obj = cls.__new__(cls)
        obj.context = SimpleNamespace(scope=scope, desktop_token=scope.token)
        obj.instance = instance
        obj.recovery_from = None if state_instance == instance else state_instance
        obj.runtime = RUNTIME/'instances'/instance
        obj.state = STATE/scope.token/state_instance
        obj.prepared = RUNTIME/'prepared'/(instance+'.json')
        obj.control = RUNTIME/'control'/instance
        obj.identities = {obj.runtime: tuple(receipt['runtime']), obj.state: tuple(receipt['state'])}
        obj.listener_identity = tuple(receipt['control'])
        obj.prepared_identity = tuple(receipt['prepared'])
        obj.prepared_digest = receipt['sha256']
        obj.listener = None
        if not partial:
            obj.verify()
        return obj

    @classmethod
    def for_recovery(cls, context, original, instance):
        original.verify()
        obj = cls(context, instance)
        obj.recovery_from = original.instance
        obj.state = original.state
        obj.identities[obj.state] = original.identities[original.state]
        return obj

    def retire_item(self, kind, *, missing_ok=False):
        """One journaled removal step; replay may find that exact item gone."""
        self.guard()
        paths = {'runtime': self.runtime, 'state': self.state,
                 'prepared': self.prepared, 'control': self.control}
        if kind not in paths:
            raise ValueError('Unknown worker removal step')
        path = paths[kind]
        parent = trusted_directory(path.parent)
        try:
            try:
                info = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
            except FileNotFoundError:
                if missing_ok:
                    return False
                raise
            if kind in {'runtime', 'state'}:
                expected = self.identities[path]
                if (not stat.S_ISDIR(info.st_mode) or info.st_uid != 0
                        or (info.st_dev, info.st_ino, stat.S_IMODE(info.st_mode), info.st_gid) != expected):
                    raise RuntimeError('A retiring worker instance directory changed')
                if not shutil.rmtree.avoids_symlink_attacks:
                    raise RuntimeError('Descriptor-relative worker cleanup is unavailable')
                shutil.rmtree(path.name, dir_fd=parent)
            else:
                expected = self.prepared_identity if kind == 'prepared' else self.listener_identity
                if ((info.st_dev, info.st_ino) != expected or info.st_uid != 0 or info.st_gid != 0
                        or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != (0o400 if kind == 'prepared' else 0o600)
                        or not (stat.S_ISREG(info.st_mode) if kind == 'prepared' else stat.S_ISSOCK(info.st_mode))):
                    raise RuntimeError('The retiring worker metadata object changed')
                if kind == 'prepared':
                    fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
                    try:
                        if hashlib.sha256(os.read(fd,65537)).hexdigest() != self.prepared_digest:
                            raise RuntimeError('The retiring worker bootstrap changed')
                    finally:
                        os.close(fd)
                os.unlink(path.name, dir_fd=parent)
            os.fsync(parent)
            return True
        finally:
            os.close(parent)

    def retire_files(self, *, state=True):
        # The journaled owner uses retire_item with an intent before every
        # step. This one-shot helper is for a fully acknowledged own fixture.
        self.verify()
        self.close_handles()
        for kind in ('runtime', *(('state',) if state else ()), 'prepared', 'control'):
            self.retire_item(kind)
