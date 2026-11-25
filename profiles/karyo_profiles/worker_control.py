# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded owner lifecycle, distinct from a provider's application protocol."""
import array
import json
import os
from pathlib import Path
import socket
import stat
import struct

from .journal import unique_object
from .worker_origin import node

LIMIT = 4096


def receive(channel, descriptors=0):
    owned = []
    try:
        data, ancillary, flags, unused = channel.recvmsg(
            LIMIT+1, socket.CMSG_SPACE(16*array.array('i').itemsize), socket.MSG_CMSG_CLOEXEC)
        for level, kind, raw in ancillary:
            if level != socket.SOL_SOCKET or kind != socket.SCM_RIGHTS:
                raise ValueError('Unexpected worker lifecycle metadata')
            integers = array.array('i')
            integers.frombytes(raw[:len(raw)-len(raw)%integers.itemsize])
            owned.extend(integers)
        if (not data or len(data) > LIMIT or flags & (socket.MSG_TRUNC|socket.MSG_CTRUNC)
                or len(owned) != descriptors):
            raise ValueError('Incomplete worker lifecycle record or descriptors')
        value = json.loads(data.decode('utf-8'), object_pairs_hook=unique_object)
        if not isinstance(value,dict):
            raise ValueError('A worker lifecycle object is required')
        result, owned = (value, owned), []
        return result
    finally:
        for descriptor in owned:
            os.close(descriptor)


def send(channel, value, descriptor=None):
    data = json.dumps(value,ensure_ascii=True,allow_nan=False,separators=(',',':')).encode('ascii')
    if not 1 <= len(data) <= LIMIT:
        raise ValueError('The worker lifecycle record exceeds its bound')
    ancillary = [] if descriptor is None else [(socket.SOL_SOCKET,socket.SCM_RIGHTS,array.array('i',[descriptor]))]
    if channel.sendmsg([data],ancillary) != len(data):
        raise OSError('Incomplete worker lifecycle publication')


class WorkerControl:
    def __init__(self, unit, files, channel):
        self.unit, self.files, self.channel = unit, files, channel
        self.instance, self.generation = files.instance, '1'
        self.endpoint = None
        self.committed = self.retiring = self.finished = False

    @classmethod
    def accept(cls, unit, files):
        unit.verify()
        files.verify()
        files.listener.settimeout(unit.declaration.declaration.ready_seconds)
        channel, unused = files.listener.accept()
        try:
            channel.settimeout(unit.declaration.declaration.ready_seconds)
            peer = struct.unpack('3i',channel.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
            if peer != (unit.identity.pid,0,unit.context.scope.dns.files.plan.gid):
                raise PermissionError('The worker lifecycle peer is not its captured process')
            obj = cls(unit,files,channel)
            obj.verify_view()
            return obj
        except BaseException:
            channel.close()
            raise

    def packet(self, kind, value):
        return {'version':1,kind:value,'instance':self.instance,'generation':self.generation}

    def verify_view(self):
        self.unit.verify()
        self.files.verify()
        root = Path('/proc',str(self.unit.identity.pid),'root')
        plan = self.unit.context.scope.dns.files.plan
        objects = ((root/'run/systemd/resolve',plan.runtime_paths[0]),
                   (root/'run/dbus/system_bus_socket',plan.bus_address.removeprefix('unix:path=')),
                   (root/str(self.files.runtime).lstrip('/'),self.files.runtime),
                   (root/str(self.files.state).lstrip('/'),self.files.state))
        if any(node(actual) != node(expected) for actual,expected in objects):
            raise PermissionError('The worker has another captured private service view')
        if node('/proc/'+str(self.unit.identity.pid)+'/ns/mnt') == node('/proc/self/ns/mnt'):
            raise PermissionError('The worker requires its prepared private mount view')

    def ready(self):
        value, descriptors = receive(self.channel,0 if self.unit.recovery else 1)
        try:
            if value != self.packet('event','ready'):
                raise ValueError('Unexpected worker readiness record')
            self.verify_view()
            if self.unit.recovery:
                return None
            descriptor = descriptors.pop()
            with socket.socket(fileno=descriptor) as listener:
                path = str(self.files.runtime/'ipc')
                peer = struct.unpack('3i',listener.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
                if (listener.getsockopt(socket.SOL_SOCKET,socket.SO_DOMAIN) != socket.AF_UNIX
                        or listener.getsockopt(socket.SOL_SOCKET,socket.SO_TYPE) != socket.SOCK_SEQPACKET
                        or listener.getsockopt(socket.SOL_SOCKET,socket.SO_ACCEPTCONN) != 1
                        or listener.getsockname() != path
                        or peer != (self.unit.identity.pid,0,self.unit.context.scope.dns.files.plan.gid)):
                    raise PermissionError('The provider endpoint is not its worker-owned listener')
                info = os.lstat(path)
                if (not stat.S_ISSOCK(info.st_mode) or info.st_nlink != 1 or info.st_uid != 0
                        or info.st_gid != self.unit.context.scope.dns.files.plan.gid
                        or stat.S_IMODE(info.st_mode) != 0o660):
                    raise PermissionError('The provider endpoint has another owner or mode')
                self.endpoint = info.st_dev,info.st_ino
                return self.endpoint
        finally:
            for descriptor in descriptors:
                os.close(descriptor)

    def commit(self):
        if self.committed or self.retiring or self.finished:
            raise RuntimeError('The worker cannot be committed in this phase')
        self.verify_view()
        send(self.channel,self.packet('operation','commit'))
        value, unused = receive(self.channel)
        if value != self.packet('event','committed'):
            raise ValueError('The worker did not acknowledge its exact committed lifetime')
        self.committed = True

    def retire(self):
        if self.finished:
            return
        if not self.retiring:
            self.channel.settimeout(self.unit.declaration.declaration.cleanup_seconds)
            send(self.channel,self.packet('operation','retire'))
            self.retiring = True

    def retired(self):
        value, unused = receive(self.channel)
        state = value.get('state')
        if value != dict(self.packet('event','retired'),state=state) or state not in {'clean','busy','failure'}:
            raise ValueError('Invalid worker retirement acknowledgement')
        return state

    def finish(self):
        # The owner must verify resource absence before this final release.
        if not self.retiring or self.finished:
            raise RuntimeError('The worker is not completing retirement')
        send(self.channel,self.packet('operation','finish'))
        self.finished = True

    def close(self):
        self.channel.close()
