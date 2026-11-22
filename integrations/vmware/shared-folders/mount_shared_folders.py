#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Manage only the fixed, explicitly owned system HGFS mount."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

MOUNTPOINT = Path('/mnt/hgfs')
RUNTIME = Path('/run/karyo-vmhgfs')


def directory(path, owner):
    if not path.exists() and not path.is_symlink():
        path.mkdir(mode=0o755 if path == MOUNTPOINT else 0o700)
    info = path.lstat()
    if (not stat.S_ISDIR(info.st_mode) or info.st_uid != owner or info.st_mode & 0o022
            or path.resolve(strict=True) != path):
        raise ValueError('Expected an owned nonsymlink directory: ' + str(path))


def mount_record(text, target):
    result = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 10 or '-' not in fields:
            raise ValueError('Malformed kernel mountinfo')
        separator = fields.index('-')
        if separator < 6 or len(fields) < separator + 4:
            raise ValueError('Incomplete kernel mountinfo')
        path = re.sub(r'\\(040|011|012|134)', lambda match: chr(int(match[1], 8)), fields[4])
        if path != str(target):
            continue
        if not fields[0].isdigit() or not re.fullmatch(r'[0-9]+:[0-9]+', fields[2]):
            raise ValueError('Invalid kernel mount identity')
        result.append({'mount_id': int(fields[0]), 'device': fields[2], 'target': str(target),
                       'fstype': fields[separator + 1], 'source': fields[separator + 2]})
    if len(result) > 1:
        raise ValueError('Stacked mounts at the HGFS target are not managed')
    return result[0] if result else None


class SharedFolders:
    def __init__(self, target=MOUNTPOINT, runtime=RUNTIME, owner=0, run=subprocess.run, mounts=None):
        self.target, self.runtime, self.owner, self.run = target, runtime, owner, run
        self.state = runtime / 'ownership.json'
        self.mounts = mounts or (lambda: Path('/proc/self/mountinfo').read_text())

    def current(self):
        return mount_record(self.mounts(), self.target)

    def load(self):
        try:
            fd = os.open(self.state, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            return None
        with os.fdopen(fd, 'rb') as stream:
            info = os.fstat(stream.fileno())
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != self.owner or info.st_nlink != 1
                    or stat.S_IMODE(info.st_mode) != 0o600 or info.st_size > 4096):
                raise ValueError('Unsafe HGFS ownership metadata')
            data = stream.read(4097)
        state = json.loads(data)
        if (not isinstance(state, dict) or set(state) != {'mount_id', 'device', 'target', 'fstype', 'source'}
                or type(state['mount_id']) is not int or state['mount_id'] < 1
                or not isinstance(state['device'], str) or not re.fullmatch(r'[0-9]+:[0-9]+', state['device'])
                or state['target'] != str(self.target) or state['fstype'] != 'fuse.vmhgfs-fuse'
                or state['source'] != 'vmhgfs-fuse'):
            raise ValueError('Unexpected HGFS ownership metadata')
        return state

    def save(self, state):
        fd = os.open(self.state, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
        with os.fdopen(fd, 'w') as stream:
            json.dump(state, stream, sort_keys=True)
            stream.write('\n')

    def start(self):
        directory(self.target.parent, self.owner)
        previous, actual = self.load(), self.current()
        if actual:
            # An existing admin/vendor mount stays under its original owner.
            # Do not adopt it, overwrite metadata or later unmount it.
            if actual != previous:
                print('HGFS target already mounted; existing mount left unchanged')
            return
        directory(self.target, self.owner)
        if previous:
            self.state.unlink()
        self.run(['/usr/bin/vmhgfs-fuse', '.host:/', str(self.target), '-o',
                  'subtype=vmhgfs-fuse,allow_other,nosuid,nodev'], check=True, timeout=10)
        actual = self.current()
        if (not actual or actual['fstype'] != 'fuse.vmhgfs-fuse' or actual['source'] != 'vmhgfs-fuse'):
            raise ValueError('Native HGFS did not establish the expected mount')
        try:
            self.save(actual)
        except BaseException:
            # Undo only the exact mount just established by this invocation.
            if self.current() == actual:
                self.run(['/usr/bin/umount', '--no-canonicalize', '--', str(self.target)], check=True, timeout=10)
            raise

    def stop(self):
        previous = self.load()
        if previous is None:
            return
        actual = self.current()
        if actual == previous:
            self.run(['/usr/bin/umount', '--no-canonicalize', '--', str(self.target)], check=True, timeout=10)
            if self.current() == actual:
                raise ValueError('Owned HGFS mount remains after unmount')
        # A disappeared/replaced mount is never removed on a stale reservation.
        self.state.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('start', 'stop'))
    action = parser.parse_args().action
    try:
        if os.geteuid() != 0 or os.getuid() != 0:
            raise ValueError('Use the optional system HGFS service')
        directory(RUNTIME.parent, 0)
        directory(RUNTIME, 0)
        fd = os.open(RUNTIME / 'lock', os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, 0o600)
        with os.fdopen(fd, 'w') as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o600:
                raise ValueError('Unsafe HGFS lock metadata')
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            manager = SharedFolders()
            manager.start() if action == 'start' else manager.stop()
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print('Karyo shared folders: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
