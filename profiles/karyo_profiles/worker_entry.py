# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed same-PID worker entry; bootstrap and control descriptors are not hints."""
import fcntl
import json
import os
from pathlib import Path
import re
import socket
import struct
import sys

from .journal import unique_object
from .model import root_file
from .storage import trusted_directory
from .worker_declaration import InstalledDeclaration
from .worker_files import RUNTIME, STATE
from .worker_origin import node


def process_start(pid):
    text = Path('/proc', str(pid), 'stat').read_text()
    prefix, separator, tail = text.rpartition(') ')
    if not separator or not prefix.startswith(str(pid)+' ('):
        raise PermissionError('The worker controller process ended')
    return int(tail.split()[19])


def source(instance):
    parent = trusted_directory(RUNTIME/'prepared')
    try:
        text = root_file(instance+'.json', dir_fd=parent)
    finally:
        os.close(parent)
    value = json.loads(text, object_pairs_hook=unique_object)
    if (not isinstance(value, dict) or type(value.get('version')) is not int or value['version'] != 1
            or value.get('role') != 'managed-worker-bootstrap'
            or value.get('worker', {}).get('id') != instance
            or value['worker'].get('unit') != 'karyo-desktop-worker-'+instance+'.service'):
        raise PermissionError('The worker has no matching prepared owner record')
    return text.encode('utf-8'), value


def verify_view(value, instance):
    expected_group = '0::/system.slice/karyo-desktop-worker-'+instance+'.service'
    if Path('/proc/self/cgroup').read_text().splitlines() != [expected_group]:
        raise PermissionError('The worker requires its fixed prepared service')
    context = value['context']
    if (list(node('/proc/self/ns/net')) != context['network']
            or list(node('/proc/self/ns/user')) != context['user']
            or list(node('/run/systemd/resolve')) != context['resolver']['directory']
            or list(node('/run/dbus/system_bus_socket')) != context['bus']['socket']):
        raise PermissionError('The worker launch context differs from its owner')
    address = context['bus']['address']
    if (not isinstance(address, str) or not address.startswith('unix:path=/run/')
            or list(node(address.removeprefix('unix:path='))) != context['bus']['socket']):
        raise PermissionError('The worker has no captured private resolver bus')
    directories = value['directories']
    desktop = value['desktop']['id']
    if not isinstance(desktop, str) or re.fullmatch('[a-f0-9]{32}', desktop) is None:
        raise PermissionError('The worker has no desktop lifetime')
    mode = value['worker'].get('mode', 'serve')
    state_instance = value['worker'].get('recovery_from', instance)
    if mode not in {'serve', 'recover'} or re.fullmatch('[a-f0-9]{32}', state_instance) is None:
        raise PermissionError('The worker has an invalid launch role')
    if mode == 'serve' and state_instance != instance:
        raise PermissionError('A serving worker cannot adopt another instance state')
    paths = {'runtime': RUNTIME/'instances'/instance, 'state': STATE/desktop/state_instance}
    for role, path in paths.items():
        recorded = directories[role]
        if recorded['path'] != str(path):
            raise PermissionError('The worker instance path changed')
        descriptor = trusted_directory(path)
        try:
            info = os.fstat(descriptor)
            if [info.st_dev, info.st_ino] != recorded['node']:
                raise PermissionError('The captured worker instance directory changed')
        finally:
            os.close(descriptor)
    return address


def control_channel(value, instance):
    channel = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET | socket.SOCK_CLOEXEC)
    try:
        channel.settimeout(5)
        channel.connect(str(RUNTIME/'control'/instance))
        pid, uid, gid = struct.unpack('3i', channel.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        owner = value['owner']
        if (uid != 0 or gid != 0 or pid != owner['pid'] or process_start(pid) != owner['start']
                or Path('/proc/sys/kernel/random/boot_id').read_text().strip() != owner['boot_id']):
            raise PermissionError('The worker control channel is not its captured owner')
        channel.settimeout(None)
        return channel
    except BaseException:
        channel.close()
        raise


def enter(instance):
    if os.getuid() != 0 or os.geteuid() != 0:
        raise PermissionError('Only the prepared root worker service may enter')
    data, value = source(instance)
    if os.getgid() != value['context']['service_gid'] or os.getegid() != value['context']['service_gid']:
        raise PermissionError('The worker requires its captured private service group')
    address = verify_view(value, instance)
    declaration = InstalledDeclaration.capture(value['declaration']['id'])
    if (declaration.digest != value['declaration']['sha256']
            or declaration.declaration.revision != value['declaration']['revision']):
        raise PermissionError('The prepared package declaration changed')
    channel = control_channel(value, instance)
    descriptor = os.memfd_create('karyo-worker-bootstrap', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    try:
        os.write(descriptor, data)
        os.lseek(descriptor, 0, os.SEEK_SET)
        fcntl.fcntl(descriptor, fcntl.F_ADD_SEALS, fcntl.F_SEAL_SEAL | fcntl.F_SEAL_WRITE |
                    fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK)
        # Reserve high descriptors before assigning either protocol slot.
        bootstrap = fcntl.fcntl(descriptor, fcntl.F_DUPFD_CLOEXEC, 10)
        try:
            lifecycle = fcntl.fcntl(channel.fileno(), fcntl.F_DUPFD_CLOEXEC, 10)
            try:
                declaration.verify()
                os.dup2(bootstrap, 3, inheritable=True)
                os.dup2(lifecycle, 4, inheritable=True)
                environment = {'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'LANG': 'C.UTF-8',
                               'DBUS_SYSTEM_BUS_ADDRESS': address}
                arguments = (declaration.declaration.recovery_arguments if value['worker'].get('mode') == 'recover'
                             else declaration.declaration.arguments)
                os.execve(arguments[0], arguments, environment)
            finally:
                os.close(lifecycle)
        finally:
            os.close(bootstrap)
    finally:
        # Success execs in place. These branches are only failed launch cleanup.
        os.close(descriptor)
        channel.close()


def main():
    if len(sys.argv) != 2 or re.fullmatch('[a-f0-9]{32}', sys.argv[1]) is None:
        return 2
    try:
        enter(sys.argv[1])
    except (OSError, RuntimeError, ValueError, PermissionError, KeyError, TypeError):
        print('karyo-worker-entry: prepared launch is unavailable', file=sys.stderr)
        return 1
    return 0
