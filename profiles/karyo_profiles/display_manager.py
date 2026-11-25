# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep LightDM's private VT allocator outside the managed child pool."""
import os
from pathlib import Path
import re
import subprocess
import sys

from .logind import SessionError
from .networks import NetworkCommand
from .services import NativeProcess, program_identity

MARKER = 'KARYO_LIGHTDM_VT_MINIMUM'
FIRST_DISPLAY_VT = 13


def configured_minimum(text):
    section = None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('['):section = line
        elif section == '[LightDM]':
            match = re.fullmatch(r'(?:[A-Z]+\s+)?minimum-vt=(\d+)', line)
            if match:return int(match[1])
    return None


def launch(arguments, environment, *, command=None):
    """Wrap the standard daemon without changing its authentication or flags."""
    if os.getuid() != 0 or os.geteuid() != 0:raise PermissionError('LightDM requires its ordinary root service')
    if len(arguments) > 64 or any(not isinstance(v, str) or len(v)>4096 or '\0' in v for v in arguments):
        raise ValueError('Invalid display-manager arguments')
    program_identity('/usr/sbin/lightdm')
    env = dict(environment);env.pop(MARKER, None)
    try:
        if command is None:
            # Query with the same root service environment/configuration flags
            # that the unchanged daemon will inherit, without exposing them in argv.
            result = subprocess.run(['/usr/sbin/lightdm', '--show-config', *arguments], env=env,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, timeout=3, check=True)
            if len(result.stdout)>256*1024:raise ValueError('Display-manager configuration output is too large')
            minimum = configured_minimum(result.stdout)
        else:
            with command.bounded(3):
                minimum = configured_minimum(command.run('/usr/sbin/lightdm', '--show-config', *arguments))
        if minimum is not None and FIRST_DISPLAY_VT <= minimum <= 63:env[MARKER] = str(minimum)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError):pass
    return ['/usr/sbin/lightdm', *arguments], env


def require_partition(*, command=None, process=None, read=None):
    command = NetworkCommand() if command is None else command
    process = NativeProcess() if process is None else process
    def status():
        return dict(line.split('=', 1) for line in command.run('/usr/bin/systemctl', 'show', 'lightdm.service',
            '--property=ActiveState', '--property=MainPID', '--property=InvocationID').splitlines())
    with command.bounded(3):
        before = status()
        if before.get('ActiveState') in {'inactive', 'failed'} and before.get('MainPID') == '0':return
        if (before.get('ActiveState') != 'active' or not before.get('MainPID', '').isdecimal()
                or int(before['MainPID']) <= 1 or re.fullmatch('[a-f0-9]{32}', before.get('InvocationID', '')) is None):
            raise SessionError('Wait for the display manager to finish its transition')
        pid = int(before['MainPID']);descriptor = process.open(pid)
        try:
            executable = program_identity('/usr/sbin/lightdm')
            started = process.read(pid, executable, 0, '/system.slice/lightdm.service', None)
            if read is None:
                with open(Path('/proc')/str(pid)/'environ', 'rb') as stream:environment = stream.read(256*1024+1)
            else:environment = read(pid)
            if not isinstance(environment, bytes) or len(environment)>256*1024:
                raise SessionError('The display-manager environment is unavailable')
            values = [v.split(b'=', 1)[1] for v in environment.split(b'\0') if v.startswith(MARKER.encode()+b'=')]
            if (len(values) != 1 or not values[0].isdigit()
                    or not FIRST_DISPLAY_VT <= int(values[0]) <= 63):
                raise SessionError('Install the Karyo LightDM integration and restart the display manager before starting child desktops')
            if (process.exited(descriptor) or status() != before
                    or process.read(pid, executable, 0, '/system.slice/lightdm.service', None) != started):
                raise SessionError('The display-manager lifetime changed during VT allocation')
        finally:process.close(descriptor)


def main():
    args, environment = launch(sys.argv[1:], os.environ)
    os.execve(args[0], args, environment)
