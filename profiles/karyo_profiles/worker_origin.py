# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture the real process on the direct activation bus, without authorizing it."""
from dataclasses import dataclass
import os
from pathlib import Path
import re
import select


def node(path):
    value = os.stat(path)
    return value.st_dev, value.st_ino


def daemon_call(bus, method, sender):
    daemon = bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus',
                            introspect=False)
    return daemon.get_dbus_method(method, 'org.freedesktop.DBus')(sender, timeout=2)


@dataclass(frozen=True)
class Origin:
    uid: int
    gid: int
    pid: int
    start: int
    executable: tuple
    network: tuple
    mount: tuple
    user: tuple
    cgroup: str
    cgroup_node: tuple


def observe(pid, uid):
    base = Path('/proc', str(pid))
    text = (base / 'stat').read_text()
    prefix, separator, tail = text.rpartition(') ')
    fields = tail.split()
    if not separator or not prefix.startswith(str(pid) + ' (') or len(fields) < 20:
        raise PermissionError('The activation process ended')
    status = {}
    for line in (base / 'status').read_text().splitlines():
        key, separator, value = line.partition(':')
        if separator:
            status[key] = value.split()
    if status.get('Uid') != [str(uid)] * 4:
        raise PermissionError('The activation process credentials changed')
    groups = status.get('Gid', [])
    if len(groups) != 4 or len(set(groups)) != 1:
        raise PermissionError('The activation process has changing group credentials')
    rows = (base / 'cgroup').read_text().splitlines()
    if len(rows) != 1 or not rows[0].startswith('0::/'):
        raise PermissionError('A unified native cgroup is required')
    cgroup = rows[0][3:]
    if '..' in Path(cgroup).parts or len(cgroup) > 4096:
        raise PermissionError('The native application cgroup is invalid')
    return Origin(uid, int(groups[0]), pid, int(fields[19]), node(base / 'exe'),
                  node(base / 'ns/net'), node(base / 'ns/mnt'), node(base / 'ns/user'),
                  cgroup, node('/sys/fs/cgroup' + cgroup))


class WorkerOrigin:
    """Pinned direct-bus origin; a separate owner must bind and authorize it.

    This is not DesktopPeer: no privileged helper, forwarded PID or proxy
    connection is accepted as an application's identity. It does not select
    a desktop from a UID or grant any execution/network permission.
    """
    def __init__(self, bus, sender):
        self.bus, self.sender = bus, sender
        self.pidfd = self.cgroup_fd = None
        self.identity = None
        if not isinstance(sender, str) or re.fullmatch(r':[0-9]+\.[0-9]+', sender) is None:
            raise PermissionError('A direct unique activation sender is required')
        uid = int(daemon_call(bus, 'GetConnectionUnixUser', sender))
        pid = int(daemon_call(bus, 'GetConnectionUnixProcessID', sender))
        if not 1000 <= uid < 65534 or pid <= 1:
            raise PermissionError('An ordinary desktop application is required')
        try:
            self.pidfd = os.pidfd_open(pid)
            self.identity = observe(pid, uid)
            self.cgroup_fd = os.open('/sys/fs/cgroup' + self.identity.cgroup,
                os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
            self.verify()
        except BaseException:
            self.close()
            raise

    def verify(self):
        value = self.identity
        if (value is None or self.pidfd is None or self.cgroup_fd is None
                or select.select([self.pidfd], [], [], 0)[0]):
            raise PermissionError('The activation process is no longer live')
        captured = os.fstat(self.cgroup_fd)
        if ((captured.st_dev, captured.st_ino) != value.cgroup_node
                or int(daemon_call(self.bus, 'GetConnectionUnixUser', self.sender)) != value.uid
                or int(daemon_call(self.bus, 'GetConnectionUnixProcessID', self.sender)) != value.pid
                or observe(value.pid, value.uid) != value
                or select.select([self.pidfd], [], [], 0)[0]):
            raise PermissionError('The direct activation origin changed')
        return value

    def close(self):
        for name in ('cgroup_fd', 'pidfd'):
            fd = getattr(self, name)
            if fd is not None:
                os.close(fd)
                setattr(self, name, None)
