# SPDX-License-Identifier: GPL-3.0-or-later
"""Pin a scoped desktop helper's actual ancestry back to its graphical login."""
from dataclasses import dataclass
import os
from pathlib import Path
import re
import select

from .logind import process_session


def application_uid(cgroup):
    match = re.fullmatch(r'/user.slice/user-([0-9]+)\.slice/user@\1\.service/app.slice/'
                         r'app-(?:[A-Za-z0-9_.@-]|\\x[0-9a-fA-F]{2})+\.(?:scope|service)', cgroup)
    if match is None or not 1000 <= int(match[1]) < 65534:
        raise PermissionError('The helper is outside a native application scope')
    return int(match[1])


@dataclass(frozen=True)
class Process:
    pid: int
    parent: int
    start: int
    uid: int
    cgroup: str
    executable: tuple


def observe(pid):
    root = Path('/proc', str(pid))
    prefix, separator, tail = (root/'stat').read_text().rpartition(') ')
    fields = tail.split()
    if not separator or not prefix.startswith(str(pid)+' (') or len(fields) < 20:
        raise PermissionError('The desktop caller ancestry ended')
    status = dict(line.split(':', 1) for line in (root/'status').read_text().splitlines() if ':' in line)
    uids = status['Uid'].split()
    if len(uids) != 4 or len(set(uids)) != 1:
        raise PermissionError('An ancestor has changing user credentials')
    rows = (root/'cgroup').read_text().splitlines()
    if len(rows) != 1 or not rows[0].startswith('0::/'):
        raise PermissionError('A unified native application cgroup is required')
    node = (root/'exe').stat()
    return Process(pid, int(fields[1]), int(fields[19]), int(uids[0]), rows[0][3:],
                   (node.st_dev, node.st_ino))


class LoginLineage:
    def __init__(self, pid, sessions, *, expected_uid=None):
        self.sessions, self.processes, self.descriptors = sessions, [], []
        self.session = None
        try:
            first = observe(pid)
            self.uid = application_uid(first.cgroup)
            if expected_uid is not None and self.uid != expected_uid:
                raise PermissionError('The scoped helper belongs to another user')
            current = first
            for index in range(64):
                if current.pid <= 1 or current.uid != (0 if index == 0 else self.uid):
                    raise PermissionError('The desktop caller ancestry changed user')
                descriptor = os.pidfd_open(current.pid)
                self.descriptors.append(descriptor)
                self.processes.append(current)
                if index:
                    try:
                        identifier = process_session(current.pid)
                    except PermissionError:
                        if application_uid(current.cgroup) != self.uid:
                            raise PermissionError('The caller left its application scope tree')
                    else:
                        session = sessions.get(identifier)
                        if (session is None or not session.active or session.uid != self.uid
                                or current.cgroup != '/user.slice/user-'+str(self.uid)+'.slice/session-'+identifier+'.scope'):
                            raise PermissionError('The caller has no active source login')
                        self.session = session
                        break
                current = observe(current.parent)
            else:
                raise PermissionError('The caller ancestry exceeds its native bound')
            self.verify()
        except BaseException:
            self.close()
            raise

    def verify(self):
        if self.session is None or not self.descriptors:
            raise PermissionError('The source login has ended')
        for process, descriptor in zip(self.processes, self.descriptors):
            if select.select([descriptor], [], [], 0)[0] or observe(process.pid) != process:
                raise PermissionError('The captured desktop caller ancestry changed')
        source = self.processes[-1]
        if process_session(source.pid) != self.session.id:
            raise PermissionError('The caller moved to another source login')
        current = self.sessions.get(self.session.id)
        before = self.session
        if (current is None or not current.active
                or (current.id, current.uid, current.name, current.vt) != (before.id, before.uid, before.name, before.vt)):
            raise PermissionError('The active source desktop changed')
        return current

    def close(self):
        for descriptor in self.descriptors:
            os.close(descriptor)
        self.descriptors = []

