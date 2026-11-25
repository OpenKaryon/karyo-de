# SPDX-License-Identifier: GPL-3.0-or-later
"""Launch unchanged NetworkManager clients in their published desktop context.

This read-only routing hint is not a broker credential or mutation authority.
The private bus applies its own credentials and the DE owns the worker lifetime.
"""
import json
import os
from pathlib import Path
import re
import socket
import stat
import struct
import subprocess
import sys

from .model import root_file
from .storage import trusted_directory

ENDPOINT = re.compile(r'/run/karyo-network-manager-([a-f0-9]{32})/client\.json')
PROGRAMS = {'nmcli':'/usr/bin/nmcli', 'connections':'/usr/bin/nm-connection-editor'}
CONTEXT_KEY = 'KARYO_NETWORK_ENDPOINT'


def validate(value):
    if (not isinstance(value, dict) or set(value) != {'version','uid','gid','network','resolver','bus','worker','invocation'}
            or type(value['version']) is not int or value['version'] != 1
            or type(value['uid']) is not int or not 1000 <= value['uid'] < 17384
            or type(value['gid']) is not int or not 0 <= value['gid'] < 2**32
            or type(value['worker']) is not int or not 1 < value['worker'] < 2**31
            or not isinstance(value['invocation'], str) or re.fullmatch('[a-f0-9]{32}',value['invocation']) is None):
        raise ValueError('Invalid desktop network-client context')
    bus = value['bus']
    if (not isinstance(bus, dict) or set(bus) != {'directory','socket','pid'}
            or type(bus['pid']) is not int or not 1 < bus['pid'] < 2**31):
        raise ValueError('Invalid desktop network-client bus')
    for parts in (value['network'],value['resolver'],bus['directory'],bus['socket']):
        if not isinstance(parts,list) or len(parts) != 2 or any(type(v) is not int or not 0 < v < 2**64 for v in parts):
            raise ValueError('Invalid desktop network-client native identity')
    return value


def serialize(value):
    return (json.dumps(validate(value),sort_keys=True,separators=(',',':'))+'\n').encode()


def unique(pairs):
    value = {}
    for key,item in pairs:
        if key in value:raise ValueError('Duplicate network-client context field')
        value[key]=item
    return value


def identity(path):
    info = os.stat(path)
    return [info.st_dev,info.st_ino]


def capture(path):
    match = ENDPOINT.fullmatch(path) if isinstance(path,str) else None
    if match is None:raise ValueError('Start the network client from its managed desktop')
    uid,gid = os.getuid(),os.getgid()
    if uid != os.geteuid() or gid != os.getegid() or not 1000 <= uid < 17384:
        raise PermissionError('The desktop network client requires its ordinary user')
    path=Path(path)
    directory=trusted_directory(path.parent)
    try:
        parent=os.fstat(directory);node=os.stat(path.name,dir_fd=directory,follow_symlinks=False)
        if (parent.st_gid != gid or stat.S_IMODE(parent.st_mode) != 0o750
                or not stat.S_ISREG(node.st_mode) or node.st_uid != 0 or node.st_gid != 0
                or stat.S_IMODE(node.st_mode) != 0o644 or node.st_nlink != 1 or node.st_size > 4096):
            raise PermissionError('The published network-client context changed')
        text=root_file(path.name,dir_fd=directory)
        value=validate(json.loads(text,object_pairs_hook=unique))
        if (value['uid'] != uid or value['gid'] != gid or identity('/proc/self/ns/net') != value['network']
                or identity('/run/systemd/resolve') != value['resolver']):
            raise PermissionError('The client is outside its published desktop network/resolver')
        # The token comes from the root-published endpoint selected by the DE;
        # neither the username nor a guessed subnet/interface selects a service.
        bus_path=Path('/run')/('karyo-resolve-bus-'+match[1])
        bus=trusted_directory(bus_path)
        try:
            info=os.fstat(bus);endpoint=os.stat('bus',dir_fd=bus,follow_symlinks=False)
            if ([info.st_dev,info.st_ino] != value['bus']['directory']
                    or [endpoint.st_dev,endpoint.st_ino] != value['bus']['socket']
                    or info.st_gid != gid or stat.S_IMODE(info.st_mode) != 0o750
                    or not stat.S_ISSOCK(endpoint.st_mode) or endpoint.st_uid != 0 or endpoint.st_gid != gid):
                raise PermissionError('The desktop network-client bus changed')
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
                connection.settimeout(1);connection.connect(str(bus_path/'bus'))
                peer=struct.unpack('3i',connection.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
                if peer != (value['bus']['pid'],0,gid):raise PermissionError('The desktop bus lifetime changed')
            address='unix:path='+str(bus_path/'bus')
            result=subprocess.run(['/usr/bin/busctl','--address='+address,'--timeout=2s','call',
                'org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus',
                'GetConnectionUnixProcessID','s','org.freedesktop.NetworkManager'],
                env={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8'},capture_output=True,text=True,timeout=3)
            if result.returncode != 0 or result.stdout.split() != ['u',str(value['worker'])]:
                raise PermissionError('The published desktop network worker is unavailable')
            after=os.stat('bus',dir_fd=bus,follow_symlinks=False)
            if (after.st_dev,after.st_ino) != (endpoint.st_dev,endpoint.st_ino):
                raise PermissionError('The desktop network socket changed during launch')
        finally:os.close(bus)
        after=os.stat(path.name,dir_fd=directory,follow_symlinks=False)
        if (after.st_dev,after.st_ino) != (node.st_dev,node.st_ino) or root_file(path.name,dir_fd=directory) != text:
            raise PermissionError('The desktop network context changed during launch')
        return address
    finally:os.close(directory)


def command(kind,arguments,environment,*,read=capture):
    if kind not in PROGRAMS:raise ValueError('Unsupported desktop network client')
    if len(arguments)>128 or any(not isinstance(v,str) or len(v)>16384 or any(c in v for c in '\0\r\n') for v in arguments):
        raise ValueError('Invalid desktop network-client arguments')
    address=read(environment.get('KARYO_NETWORK_ENDPOINT'))
    return [PROGRAMS[kind],*arguments],dict(environment,DBUS_SYSTEM_BUS_ADDRESS=address)


def manager_environment():
    """Read the native user manager, never a privileged controller operation."""
    from .networks import NetworkCommand
    uid=os.getuid()
    if uid != os.geteuid() or not 1000 <= uid < 17384:
        raise PermissionError('The network context belongs to an ordinary desktop user')
    command=NetworkCommand()
    with command.bounded(4):
        text=command.run('/usr/bin/busctl','--address=unix:path=/run/user/'+str(uid)+'/bus',
            '--json=short','--timeout=3s','get-property','org.freedesktop.systemd1',
            '/org/freedesktop/systemd1','org.freedesktop.systemd1.Manager','Environment')
    value=json.loads(text,object_pairs_hook=unique)
    if (not isinstance(value,dict) or set(value) != {'type','data'} or value['type'] != 'as'
            or not isinstance(value['data'],list) or len(value['data']) > 1024
            or any(not isinstance(item,str) or len(item)>16384 for item in value['data'])):
        raise ValueError('Invalid native user-manager environment reply')
    return value['data']


def session_context(environment,*,query=manager_environment):
    """Select just one routing hint; unavailable VPNs must not end the desktop.

    The hint grants no authority. Actual client launch separately validates its
    root-published objects and the client's own kernel/resolver context.
    """
    path=environment.get(CONTEXT_KEY)
    if path is None:
        from .networks import NetworkError
        try:values=query()
        except (OSError,NetworkError):return ''  # Ordinary unmanaged/standalone sessions need no service.
        values=[item.split('=',1)[1] for item in values if item.startswith(CONTEXT_KEY+'=')]
        if not values:return ''
        if len(values) != 1:raise ValueError('Ambiguous network context in the user manager')
        path=values[0]
    if not isinstance(path,str) or ENDPOINT.fullmatch(path) is None:
        raise ValueError('Invalid desktop network-context hint')
    return path


def main(kind):
    try:
        if kind == 'context':
            if sys.argv[1:]:raise ValueError('The network-context helper accepts no arguments')
            path=session_context(os.environ)
            if path:print(path)
            return 0
        arguments,environment=command(kind,sys.argv[1:],os.environ)
        os.execve(arguments[0],arguments,environment)
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError):
        print('karyo-network: this desktop network service is unavailable; reopen the client from its desktop.',file=sys.stderr)
        return 1
