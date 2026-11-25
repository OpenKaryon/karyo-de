# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit networkd cooperation before creating desktop routing resources."""
import os
from pathlib import Path
import pwd
import re
import subprocess
import sys

from .networks import NetworkCommand, NetworkError
from .services import NativeProcess, program_identity

MARKER = 'KARYO_NETWORKD_FOREIGN_ROUTING'
PROGRAM = '/usr/lib/systemd/systemd-networkd'
OPTIONS = ('ManageForeignRoutes', 'ManageForeignRoutingPolicyRules')


def configured(text):
    section = '';values = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(('#', ';')):continue
        if line.startswith('['):section = line
        elif section == '[Network]':
            key, separator, value = line.partition('=')
            if separator and key.strip() in OPTIONS:values[key.strip()] = value.strip().lower()
    return all(values.get(name) in ('no', 'false', 'off', '0') for name in OPTIONS)


def launch(arguments, environment, *, configuration=None):
    if len(arguments)>64 or any(not isinstance(v,str) or len(v)>4096 or '\0' in v for v in arguments):
        raise ValueError('Invalid networkd arguments')
    program_identity(PROGRAM)
    env = dict(environment);env.pop(MARKER,None)
    try:
        if configuration is None:
            p = subprocess.run(['/usr/bin/systemd-analyze','cat-config','systemd/networkd.conf'],env=env,
                stdin=subprocess.DEVNULL,capture_output=True,text=True,check=True,timeout=3)
            configuration = p.stdout
        if len(configuration)<=256*1024 and configured(configuration):env[MARKER]='1'
    except (OSError,RuntimeError,ValueError,subprocess.SubprocessError):pass
    return [PROGRAM,*arguments],env


def require_cooperation(*,command=None,process=None,read=None,service_uid=None):
    command = NetworkCommand() if command is None else command
    process = NativeProcess() if process is None else process
    def status():
        return dict(line.split('=',1) for line in command.run('/usr/bin/systemctl','show','systemd-networkd.service',
            '--property=ActiveState','--property=MainPID','--property=InvocationID').splitlines())
    with command.bounded(3):
        before = status()
        if before.get('ActiveState') in {'inactive','failed'} and before.get('MainPID')=='0':return
        if (before.get('ActiveState')!='active' or not before.get('MainPID','').isdecimal()
                or int(before['MainPID'])<=1 or re.fullmatch('[a-f0-9]{32}',before.get('InvocationID','')) is None):
            raise NetworkError('Wait for the physical network service to finish its transition')
        pid = int(before['MainPID']);descriptor = process.open(pid)
        try:
            uid = pwd.getpwnam('systemd-network').pw_uid if service_uid is None else service_uid
            executable = program_identity(PROGRAM)
            net = Path('/proc/self/ns/net').stat();namespace = (net.st_dev,net.st_ino)
            started = process.read(pid,executable,uid,'/system.slice/systemd-networkd.service',namespace)
            if read is None:
                with open(Path('/proc')/str(pid)/'environ','rb') as stream:environment = stream.read(256*1024+1)
            else:environment = read(pid)
            if not isinstance(environment,bytes) or len(environment)>256*1024:
                raise NetworkError('The physical network service environment is unavailable')
            values = [v for v in environment.split(b'\0') if v.startswith(MARKER.encode()+b'=')]
            if values != [MARKER.encode()+b'=1']:
                raise NetworkError('Install the optional networkd cooperation package and explicitly restart networkd before enabling private desktops')
            if (process.exited(descriptor) or status()!=before
                    or process.read(pid,executable,uid,'/system.slice/systemd-networkd.service',namespace)!=started):
                raise NetworkError('The physical network service lifetime changed')
        finally:process.close(descriptor)


def main():
    args,environment = launch(sys.argv[1:],os.environ)
    os.execve(args[0],args,environment)
