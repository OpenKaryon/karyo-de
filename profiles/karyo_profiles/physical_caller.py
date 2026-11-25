# SPDX-License-Identifier: GPL-3.0-or-later
"""Original system-bus caller and active local login for physical controls."""
from pathlib import Path

from .logind import Sessions


def start_time(pid):
    text = Path('/proc', str(pid), 'stat').read_text()
    head, separator, tail = text.rpartition(') ')
    fields = tail.split()
    if not separator or not head.startswith(str(pid) + ' (') or len(fields) < 20:
        raise PermissionError('The network caller process ended')
    return int(fields[19])


class PhysicalCaller:
    def __init__(self, bus, sender, *, sessions=None, start=start_time):
        if not isinstance(sender, str) or not sender.startswith(':'):
            raise PermissionError('A unique system-bus caller is required')
        self.bus, self.sender, self.start = bus, sender, start
        self.sessions = Sessions() if sessions is None else sessions
        self.uid = int(bus.get_unix_user(sender))
        self.pid = self.process_id()
        if not 1000 <= self.uid < 65534 or self.pid <= 1:
            raise PermissionError('A desktop application is required')
        self.started = self.start(self.pid)
        self.session = self.active()
        self.verify()

    def active(self):
        matches = [s for s in self.sessions.all() if s.uid == self.uid and s.active and not s.locked]
        if len(matches) != 1:
            raise PermissionError('The calling desktop is not active and unlocked')
        return matches[0]

    def process_id(self):
        obj = self.bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus', introspect=False)
        return int(obj.get_dbus_method('GetConnectionUnixProcessID', 'org.freedesktop.DBus')(self.sender, timeout=2))

    def verify(self):
        if (int(self.bus.get_unix_user(self.sender)) != self.uid
                or self.process_id() != self.pid
                or self.start(self.pid) != self.started or self.active() != self.session):
            raise PermissionError('The active network caller changed')

