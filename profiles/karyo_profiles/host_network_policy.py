# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep real host-bus credentials while selecting private network services.

Only a captured offline profile UID receives this temporary D-Bus rule. It does
not proxy calls, impersonate root or replace the host's system-bus socket.
"""
from dataclasses import asdict
import os
from pathlib import Path
import stat

from .journal import JournalRetained
from .journal_dns_record import identity
from .journal_network import offline
from .model import root_file
from .services import host_controller
from .storage import trusted_directory


DIRECTORY = Path('/run/karyo/host_network_policies')
LOADER = Path('/usr/share/dbus-1/system.d/org.karyo.ProfileNetwork.conf')
LOADER_CONTENTS = '''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <!-- Empty unless the opt-in profile controller owns an active UID rule. -->
  <includedir>/run/karyo/host_network_policies</includedir>
</busconfig>
'''
NETWORK_DESTINATIONS = ('org.freedesktop.NetworkManager', 'org.freedesktop.resolve1',
                        'org.freedesktop.network1')


def validate_host_network_policy(value):
    if not isinstance(value, dict) or set(value) != {'pending', 'directory', 'file'}:
        raise ValueError('Invalid profile host-network policy receipt')
    if value['pending'] not in {None, 'publish', 'reload', 'remove'}:
        raise ValueError('Invalid profile host-network policy transition')
    identity(value['directory'])
    if value['file'] is None:
        if value['pending'] != 'publish':raise ValueError('Missing host-network policy acknowledgement')
    else:identity(value['file'])


class HostNetworkPolicy:
    def __init__(self, journal, network, *, accounts, sessions):
        self.journal, self.network = journal, network
        self.token = network.spec.token
        self.accounts, self.sessions = accounts, sessions
        self.name, self.staged = self.token + '.conf', self.token + '.pending'
        self.directory = None
        self.contents = ('<busconfig><policy user="' + str(network.spec.uid) + '">' + ''.join(
            '<deny send_destination_prefix="' + name + '"/>' for name in NETWORK_DESTINATIONS)
            + '</policy></busconfig>\n').encode()

    def guard(self):
        host_controller()
        if not self.network.store._locked:
            raise PermissionError('Host-network policy requires the profile configuration lock')
        parent = trusted_directory(LOADER.parent)
        try:
            if root_file(LOADER.name, dir_fd=parent) != LOADER_CONTENTS:
                raise JournalRetained('The packaged host-network policy loader is unavailable or changed')
        finally:os.close(parent)
        if self.directory is not None:
            current = trusted_directory(DIRECTORY)
            try:
                node = os.fstat(current)
                if [node.st_dev, node.st_ino] != self.directory_identity():
                    raise JournalRetained('The published host-network policy directory changed')
            finally:os.close(current)
        account = self.accounts.by_uid(self.network.spec.uid)
        if asdict(account) != self.journal.record(self.token)['account']:
            raise JournalRetained('The host-network policy account changed')
        return account

    def reload(self):
        self.network.command.run('/usr/bin/busctl', '--address=unix:path=/run/dbus/system_bus_socket',
            '--timeout=5s', 'call', 'org.freedesktop.DBus', '/org/freedesktop/DBus',
            'org.freedesktop.DBus', 'ReloadConfig')

    def update(self, **values):
        before = self.journal.record(self.token)
        self.journal.change(before, dict(before, host_network_policy=dict(before['host_network_policy'], **values)))

    def open(self, *, create=False):
        if self.directory is not None:raise RuntimeError('The policy directory is already open')
        self.directory = trusted_directory(DIRECTORY, create=create)

    def directory_identity(self):
        node = os.fstat(self.directory)
        return [node.st_dev, node.st_ino]

    def node(self, name):
        try:fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=self.directory)
        except FileNotFoundError:return None
        try:
            node = os.fstat(fd)
            if (not stat.S_ISREG(node.st_mode) or node.st_uid != 0 or node.st_gid != 0
                    or stat.S_IMODE(node.st_mode) != 0o644 or node.st_nlink != 1
                    or os.read(fd, len(self.contents) + 1) != self.contents):
                raise JournalRetained('The captured host-network policy changed')
            return [node.st_dev, node.st_ino]
        finally:os.close(fd)

    def captured_name(self, value):
        if self.directory_identity() != value['directory']:
            raise JournalRetained('The captured host-network policy directory changed')
        if value['file'] is None:raise JournalRetained('Unacknowledged host-network policy creation requires review')
        nodes = [(name, self.node(name)) for name in (self.name, self.staged)]
        present = [(name, node) for name, node in nodes if node is not None]
        if not present and value['pending'] == 'remove':return None
        if (len(present) != 1 or present[0][1] != value['file']
                or present[0][0] == self.staged and value['pending'] != 'publish'):
            raise JournalRetained('The captured host-network policy publication changed')
        return present[0][0]

    @classmethod
    def start(cls, journal, dns):
        from .logind import Sessions
        owner = cls(journal, dns.network.network, accounts=dns.files.accounts, sessions=Sessions())
        try:
            account = owner.guard()
            before = journal.record(owner.token)
            if ({'session', 'main_login', 'host_bus', 'host_network_policy'} & before.keys()
                    or before['dns']['pending'] is not None):
                raise JournalRetained('Host-network policy starts before graphical consumers')
            dns.files.verify();dns.services.verify()
            offline(account, owner.accounts, owner.sessions)
            owner.open(create=True)
            if any(os.path.lexists(DIRECTORY / name) for name in (owner.name, owner.staged)):
                raise JournalRetained('An existing profile host-network policy is retained')
            journal.change(before, dict(before, host_network_policy={
                'pending':'publish', 'directory':owner.directory_identity(), 'file':None}))
            fd = os.open(owner.staged, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                         0o600, dir_fd=owner.directory)
            with os.fdopen(fd, 'wb') as stream:
                stream.write(owner.contents);stream.flush();os.fchmod(stream.fileno(), 0o644);os.fsync(stream.fileno())
            os.fsync(owner.directory)
            owner.update(file=owner.node(owner.staged))
            os.rename(owner.staged, owner.name, src_dir_fd=owner.directory, dst_dir_fd=owner.directory)
            os.fsync(owner.directory)
            owner.update(pending='reload')
            owner.reload()
            owner.update(pending=None)
            owner.verify()
            return owner
        except BaseException:
            owner.release_handles()
            raise

    @classmethod
    def restore(cls, journal, network, *, accounts, sessions):
        owner = cls(journal, network, accounts=accounts, sessions=sessions)
        try:
            owner.guard();owner.open()
            value = journal.record(owner.token)['host_network_policy']
            validate_host_network_policy(value)
            owner.captured_name(value)
            return owner
        except BaseException:
            owner.release_handles()
            raise

    def verify(self):
        self.guard()
        value = self.journal.record(self.token)['host_network_policy']
        if value['pending'] is not None or self.captured_name(value) != self.name:
            raise JournalRetained('The profile host-network policy is not acknowledged')

    def close(self):
        account = self.guard()
        before = self.journal.record(self.token)
        if {'session', 'main_login', 'host_bus'} & before.keys():
            raise JournalRetained('Graphical consumers must close before their host-network policy')
        offline(account, self.accounts, self.sessions)
        name = self.captured_name(before['host_network_policy'])
        self.update(pending='remove')
        if name is not None:
            os.unlink(name, dir_fd=self.directory);os.fsync(self.directory)
        self.reload()
        before = self.journal.record(self.token)
        after = dict(before);after.pop('host_network_policy')
        self.journal.change(before, after)
        self.release_handles()

    def release_handles(self):
        if self.directory is not None:os.close(self.directory);self.directory = None


def recover_host_network_policies(journal, store, *, accounts, sessions, command=None, exclude=()):
    from .journal_network import restore_network
    for record in journal.snapshot()['profiles']:
        if record['token'] in exclude or 'host_network_policy' not in record:continue
        with store.locked():
            network = restore_network(store, record, command, require_empty=False)
            if network is None:raise JournalRetained('The host-network policy lost its captured profile')
            owner = None
            try:
                owner = HostNetworkPolicy.restore(journal, network, accounts=accounts, sessions=sessions)
                owner.close()
            finally:
                if owner is not None:owner.release_handles()
                if network.fd is not None:os.close(network.fd);network.fd = None


def retire_host_network_policy(journal, network, *, accounts, sessions):
    if 'host_network_policy' not in journal.record(network.spec.token):return
    owner = HostNetworkPolicy.restore(journal, network, accounts=accounts, sessions=sessions)
    try:owner.close()
    finally:owner.release_handles()
