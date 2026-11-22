# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded, volatile text/PNG sharing authorized by ordinary local family UIDs."""
from collections import OrderedDict
from dataclasses import astuple
import hashlib
import os
from pathlib import Path
import socket
import socketserver
import stat
import struct
import threading

from .credentials import LocalAccounts
from .storage import ConfigStore, trusted_directory

SOCKET = '/run/karyo/clipboard.sock'
LIMIT = 8 * 1024 * 1024
FRAME_LIMIT = LIMIT + 128
CACHE_LIMIT = 32 * 1024 * 1024
CACHE_FAMILIES = 16
ZERO = bytes(32)
TYPES = (b'text/plain;charset=utf-8', b'image/png')


def read_exact(connection, size):
    result = bytearray()
    while len(result) < size:
        block = connection.recv(min(65536, size-len(result)))
        if not block:
            raise ValueError('Incomplete clipboard frame')
        result.extend(block)
    return bytes(result)


def receive(connection):
    size = struct.unpack('!I', read_exact(connection, 4))[0]
    if not 34 <= size <= FRAME_LIMIT:
        raise ValueError('Unsupported clipboard frame size')
    return read_exact(connection, size)


def send(connection, data):
    if not 34 <= len(data) <= FRAME_LIMIT:
        raise ValueError('Unsupported clipboard response size')
    connection.sendall(struct.pack('!I', len(data)))
    connection.sendall(data)


def payload(value):
    mime, separator, data = value.partition(b'\0')
    if not separator or mime not in TYPES or len(data) > LIMIT:
        raise ValueError('Unsupported clipboard content')
    return value


def request(operation, known=ZERO, value=b''):
    if not isinstance(operation, int) or operation not in b'GUSC' or len(known) != 32:
        raise ValueError('Unsupported clipboard operation')
    if operation in b'US':
        payload(value)
    elif value:
        raise ValueError('Unexpected clipboard content')
    return b'\1' + bytes([operation]) + known + value


def parse_response(data):
    if len(data) < 34 or data[:1] != b'\1' or data[1] not in b'DCNEZ':
        raise ValueError('Unsupported clipboard response')
    kind, revision, value = data[1:2], data[2:34], data[34:]
    if kind == b'D':
        payload(value)
        if revision != hashlib.sha256(b'D'+value).digest():
            raise ValueError('Clipboard content checksum mismatch')
    elif value:
        raise ValueError('Unexpected clipboard response data')
    if kind in (b'E', b'Z') and revision != ZERO:
        raise ValueError('Unexpected clipboard response revision')
    if kind == b'C' and revision != hashlib.sha256(b'C').digest():
        raise ValueError('Clipboard clear checksum mismatch')
    return kind, revision, value


def generation(path):
    info = Path(path).lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
        raise ValueError('Unsafe local account database')
    return info.st_dev, info.st_ino, info.st_mtime_ns, info.st_ctime_ns


class Families:
    def __init__(self, store=None, accounts=None):
        self.store = ConfigStore() if store is None else store
        self.accounts = LocalAccounts() if accounts is None else accounts

    def resolve(self, uid):
        before = generation(self.accounts.passwd)
        ownership = self.store.ownership()
        epoch = (before, hashlib.sha256(ownership.serialize().encode()).digest())
        family = None
        if type(uid) is int and 1000 <= uid < 65534:
            try:
                caller = self.accounts.by_uid(uid)
                names = ownership.family(caller.name)
                if len(names) > 1:
                    family = tuple(astuple(self.accounts.account(name)) for name in names)
            except ValueError:
                pass  # Missing, duplicate or non-login accounts are not members.
        if generation(self.accounts.passwd) != before:
            raise ValueError('Accounts changed during clipboard authorization')
        return epoch, family


class Clipboards:
    def __init__(self, families=None):
        self.families = Families() if families is None else families
        self.epoch = None
        self.values = OrderedDict()
        self.lock = threading.Lock()

    def _family(self, uid):
        try:
            epoch, family = self.families.resolve(uid)
        except (OSError, ValueError):
            self.values.clear()
            self.epoch = None
            return None
        if epoch != self.epoch:
            self.values.clear()
            self.epoch = epoch
        return family

    def authorized(self, uid):
        with self.lock:
            return self._family(uid) is not None

    def exchange(self, uid, data):
        if len(data) < 34 or data[:1] != b'\1' or data[1] not in b'GUSC':
            raise ValueError('Unsupported clipboard request')
        operation, known, value = data[1], data[2:34], data[34:]
        if operation in b'US':
            payload(value)
        elif value:
            raise ValueError('Unexpected clipboard request data')
        with self.lock:
            family = self._family(uid)
            if family is None:
                return b'\1E'+ZERO
            if operation == ord('U') or operation == ord('S') and family not in self.values:
                self.values[family] = (hashlib.sha256(b'D'+value).digest(), value)
            elif operation == ord('C'):
                self.values[family] = (hashlib.sha256(b'C').digest(), b'')
            if family not in self.values:
                return b'\1Z'+ZERO
            self.values.move_to_end(family)
            while len(self.values) > CACHE_FAMILIES or sum(len(v[1]) for v in self.values.values()) > CACHE_LIMIT:
                self.values.popitem(last=False)
            revision, value = self.values[family]
            kind = b'N' if revision == known else b'D' if value else b'C'
            return b'\1'+kind+revision+(value if kind == b'D' else b'')


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(2)
        try:
            unused_pid, uid, unused_gid = struct.unpack('3i', self.request.getsockopt(
                socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize('3i')))
            # Refuse outsiders before accepting a potentially large body.
            if not self.server.clipboards.authorized(uid):
                send(self.request, b'\1E'+ZERO)
                return
            send(self.request, self.server.clipboards.exchange(uid, receive(self.request)))
        except (OSError, ValueError, struct.error):
            pass  # Neither selection contents nor request fields are logged.


class Server(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True
    block_on_close = False

    def __init__(self, listener, clipboards=None):
        super().__init__(SOCKET, Handler, bind_and_activate=False)
        self.socket.close()
        self.socket = listener
        self.clipboards = Clipboards() if clipboards is None else clipboards
        self.workers = threading.BoundedSemaphore(4)

    def process_request(self, request_socket, address):
        if not self.workers.acquire(blocking=False):
            self.shutdown_request(request_socket)
            return
        try:
            super().process_request(request_socket, address)
        except BaseException:
            self.workers.release()
            raise

    def process_request_thread(self, request_socket, address):
        try:
            super().process_request_thread(request_socket, address)
        finally:
            self.workers.release()

    def handle_error(self, unused_request, unused_address):
        pass


def socket_path():
    directory = trusted_directory(Path(SOCKET).parent)
    try:
        info = os.stat(Path(SOCKET).name, dir_fd=directory, follow_symlinks=False)
        if not stat.S_ISSOCK(info.st_mode) or info.st_uid != 0 or stat.S_IMODE(info.st_mode) != 0o666:
            raise PermissionError('Family clipboard socket is unavailable or changed')
    finally:
        os.close(directory)


def serve():
    if os.getuid() == 0 or os.getuid() != os.geteuid() or os.getgid() != os.getegid():
        raise PermissionError('The family clipboard service must run without root')
    if (os.environ.get('LISTEN_PID') != str(os.getpid()) or os.environ.get('LISTEN_FDS') != '1'
            or os.environ.get('LISTEN_FDNAMES') != 'clipboard'):
        raise PermissionError('The family clipboard needs its fixed systemd listener')
    socket_path()
    listener = socket.socket(fileno=3)
    if (listener.family != socket.AF_UNIX or listener.type != socket.SOCK_STREAM
            or listener.getsockname() != SOCKET or not listener.getsockopt(socket.SOL_SOCKET, socket.SO_ACCEPTCONN)):
        raise ValueError('Unexpected family clipboard listener')
    with Server(listener) as server:
        server.serve_forever()
