#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Release the verified VMware file-transfer connection only during shutdown."""
from dataclasses import dataclass
import os
from pathlib import Path
import subprocess

MOUNT = '/run/vmblock-fuse'
MOUNTINFO = Path('/proc/self/mountinfo')
FUSE_CONTROL = Path('/sys/fs/fuse/connections')


@dataclass(frozen=True)
class Connection:
    mount_id: str
    number: str


def shutdown_pending(jobs):
    return any(line.split()[1:3] == ['shutdown.target', 'start']
               for line in jobs.splitlines())


def connection(mountinfo):
    matches = []
    for line in mountinfo.splitlines():
        left, separator, right = line.partition(' - ')
        mount, backend = left.split(), right.split()
        if separator and mount[4:5] == [MOUNT]:
            matches.append((mount, backend))
    if len(matches) != 1:
        return None
    mount, backend = matches[0]
    if mount[3] != '/' or backend[:2] != ['fuse.vmware-vmblock', 'vmware-vmblock']:
        return None
    major, separator, minor = mount[2].partition(':')
    if not separator or major != '0' or not minor.isascii() or not minor.isdecimal():
        return None
    return Connection(mount[0], minor)


def abort_vmblock(read_mountinfo=MOUNTINFO.read_text, control=FUSE_CONTROL):
    captured = connection(read_mountinfo())
    if captured is None:
        return None
    # Pin the control directory, then recheck kernel metadata. Never traverse
    # the file-transfer mount, whose requests may be the reason shutdown stalls.
    directory = None
    try:
        directory = os.open(control / captured.number,
                            os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
        if connection(read_mountinfo()) != captured:
            return None
        fd = os.open('abort', os.O_WRONLY | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=directory)
        with os.fdopen(fd, 'w') as stream:
            stream.write('1\n')
    except FileNotFoundError:
        return None  # The connection was already unmounted.
    finally:
        if directory is not None:
            os.close(directory)
    return captured.number


def main():
    jobs = subprocess.check_output(
        ['/usr/bin/systemctl', 'list-jobs', '--full', '--no-legend', '--no-pager', '--plain'],
        stdin=subprocess.DEVNULL, text=True, timeout=3)
    if not shutdown_pending(jobs):
        return 0
    number = abort_vmblock()
    if number is not None:
        print(f'Released VMware vmblock FUSE connection {number} before session shutdown',
              flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
