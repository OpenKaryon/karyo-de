# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture the actual root PAM caller, never a PID supplied in a request."""
import os
from pathlib import Path
import socket
import struct

from .journal import JournalRetained
from .journal_main_record import validate_origin
from .services import NativeProcess, program_identity


def object_identity(path):
    info = os.stat(path)
    return info.st_dev, info.st_ino


class MainPeer:
    def __init__(self, origin, *, process=None):
        validate_origin(origin)
        self.origin = dict(origin)
        self.process = NativeProcess() if process is None else process
        self.fd = None
        try:
            self.fd = self.process.open(origin["pid"])
            self.verify()
        except BaseException:
            self.close()
            raise

    @classmethod
    def capture(cls, connection, service, *, program="/usr/sbin/lightdm", cgroup="/system.slice/lightdm.service"):
        # Tests inject a fixed root-owned harness and its transient unit here.
        # Neither the program, cgroup nor PID is accepted from the wire packet.
        pid, uid, gid = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if uid != 0 or gid != 0:
            raise PermissionError("Main PAM entry requires its privileged session parent")
        process = NativeProcess()
        executable = program_identity(program)
        start = process.read(pid, executable, 0, cgroup, object_identity("/proc/1/ns/net"))
        return cls({"pid": pid, "start": start, "exe": list(executable), "cgroup": cgroup,
                    "service": service}, process=process)

    def verify(self, *, cgroup=None, network=None):
        value = self.origin
        if self.fd is None or self.process.exited(self.fd):
            raise JournalRetained("The captured main PAM parent has exited")
        pid = value["pid"]
        if object_identity("/proc/" + str(pid) + "/ns/user") != object_identity("/proc/1/ns/user"):
            raise JournalRetained("The captured main PAM user namespace changed")
        if cgroup is None:
            rows = Path("/proc", str(pid), "cgroup").read_text().splitlines()
            if len(rows) != 1 or not rows[0].startswith("0::"):
                raise JournalRetained("Invalid main PAM parent cgroup")
            cgroup = rows[0][3:]
        start = self.process.read(pid, tuple(value["exe"]), 0, cgroup, network)
        if start != value["start"] or self.process.exited(self.fd):
            raise JournalRetained("The captured main PAM parent identity changed")

    def matches(self, connection):
        pid, uid, gid = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if pid != self.origin["pid"] or uid != 0 or gid != 0:
            raise PermissionError("The PAM request is not from the captured main parent")
        self.verify()

    def close(self):
        if self.fd is not None:
            self.process.close(self.fd)
            self.fd = None
