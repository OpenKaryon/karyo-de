# SPDX-License-Identifier: GPL-3.0-or-later
"""Authenticate a root polkit helper from its Unix peer and kernel login scope."""
from dataclasses import dataclass
import os
import socket
import struct

from .credentials import LocalAccounts
from .logind import Sessions, process_session
from .services import NativeProcess, program_identity


@dataclass(frozen=True)
class PeerIdentity:
    pid: int
    start: int
    session: tuple
    account: object
    cgroup: str


class DesktopPeer:
    def __init__(self, connection, *, accounts=None, sessions=None, process=None,
                 identify=process_session):
        self.accounts = LocalAccounts() if accounts is None else accounts
        self.sessions = Sessions() if sessions is None else sessions
        self.process = NativeProcess() if process is None else process
        self.identify = identify
        self.fd = None
        self.lineage = None
        self.identity = None
        pid, uid, gid = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if pid <= 1 or uid != 0 or gid != 0:
            raise PermissionError("Only the privileged desktop helper may contact the host controller")
        try:
            self.fd = self.process.open(pid)
            self.executable = program_identity("/usr/bin/python3")
            try:
                identifier = self.identify(pid)
                current = self.sessions.get(identifier)
                cgroup = None
            except PermissionError:
                from .login_lineage import LoginLineage
                self.lineage = LoginLineage(pid, self.sessions)
                current = self.lineage.verify()
                cgroup = self.lineage.processes[0].cgroup
            if current is None or not current.active:
                raise PermissionError("The helper has no active local graphical session")
            account = self.accounts.by_uid(current.uid)
            if account.name != current.name:
                raise PermissionError("The helper login account is inconsistent")
            cgroup = cgroup or "/user.slice/user-" + str(account.uid) + ".slice/session-" + current.id + ".scope"
            self.host_user_namespace(pid)
            start = self.process.read(pid, self.executable, 0, cgroup, None)
            self.identity = PeerIdentity(pid, start, self.session_identity(current), account, cgroup)
            self.verify()
        except BaseException:
            self.close()
            raise

    @staticmethod
    def session_identity(value):
        return value.id, value.uid, value.name, value.vt

    @staticmethod
    def host_user_namespace(pid):
        peer, host = os.stat("/proc/" + str(pid) + "/ns/user"), os.stat("/proc/1/ns/user")
        if (peer.st_dev, peer.st_ino) != (host.st_dev, host.st_ino):
            raise PermissionError("The desktop helper is not in the host user namespace")

    def verify(self):
        identity = self.identity
        if identity is None or self.fd is None or self.process.exited(self.fd):
            raise PermissionError("The desktop helper process has ended")
        if (self.lineage.verify().id if self.lineage is not None else self.identify(identity.pid)) != identity.session[0]:
            raise PermissionError("The desktop helper moved to another login scope")
        self.host_user_namespace(identity.pid)
        if self.process.read(identity.pid, self.executable, 0, identity.cgroup, None) != identity.start:
            raise PermissionError("The desktop helper process identity changed")
        current = self.sessions.get(identity.session[0])
        if (current is None or not current.active or self.session_identity(current) != identity.session
                or self.accounts.by_uid(identity.account.uid) != identity.account
                or self.process.exited(self.fd)):
            raise PermissionError("The active desktop caller changed")
        return identity.account

    def close(self):
        if self.lineage is not None:
            self.lineage.close()
            self.lineage = None
        if self.fd is not None:
            self.process.close(self.fd)
            self.fd = None
