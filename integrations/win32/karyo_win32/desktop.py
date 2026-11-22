# SPDX-License-Identifier: GPL-3.0-or-later
"""User-local launcher and nonexecuting PE icon registration."""
import os,re,shutil,struct,subprocess,tempfile,time
from pathlib import Path
from .policy import LaunchError
from .storage import atomic_bytes,directory,secure_directory,write_json

def desktop_escape(value):
    return value.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')
def extract_icon(executable_path, destination):
    """Read the primary PE icon without executing Windows code."""
    wrestool = shutil.which('wrestool')
    icotool = shutil.which('icotool')
    if not wrestool or not icotool:
        return False
    try:
        with tempfile.TemporaryDirectory(prefix='.icon-', dir=destination.parent) as work:
            directory = Path(work)
            icon = directory / 'primary.ico'
            with icon.open('wb') as output:
                result = subprocess.run([wrestool, '--extract', '--type=14', executable_path],
                                        stdout=output, stderr=subprocess.DEVNULL, timeout=4)
            if result.returncode or not 0 < icon.stat().st_size <= 16 * 1024 * 1024:
                return False
            result = subprocess.run([icotool, '--extract', '--output', work, str(icon)],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=4)
            if result.returncode:
                return False
            candidates = []
            for png in directory.glob('primary_*.png'):
                with png.open('rb') as stream:
                    header = stream.read(24)
                if len(header) == 24 and header.startswith(b'\x89PNG\r\n\x1a\n'):
                    width, height = struct.unpack_from('>II', header, 16)
                    if 0 < width <= 256 and 0 < height <= 256:
                        candidates.append((width * height, png.stat().st_size, png))
            if not candidates:
                return False
            max(candidates)[2].replace(destination)
            return True
    except (OSError, subprocess.TimeoutExpired):
        return False
def register_application(context, environment, home):
    storage = Path(context['storage']) / 'applications'
    secure_directory(storage)
    write_json(storage / (context['applicationId'] + '.json'), {
        'version': 1, 'applicationId': context['applicationId'], 'ownerUid': context['ownerUid'], 'file': context['file'],
    })
    data_home = Path(environment.get('XDG_DATA_HOME') or home / '.local/share')
    if not data_home.is_absolute():
        raise LaunchError('XDG_DATA_HOME должен быть абсолютным путём.')
    menu = data_home / 'applications'
    directory(menu,private=False)
    executable_path = Path(context['file'])
    name = desktop_escape(executable_path.stem)
    window_class = desktop_escape(executable_path.name.lower())
    icons = Path(context['storage']) / 'icons'
    secure_directory(icons)
    icon_path = icons / (context['applicationId'] + '.png')
    icon = desktop_escape(str(icon_path)) if extract_icon(str(executable_path), icon_path) else 'application-x-ms-dos-executable'
    desktop = menu / ('karyo-win32-' + context['applicationId'] + '.desktop')
    content = (
        '[Desktop Entry]\nType=Application\n'
        f'Name={name} (Windows)\n'
        f'Exec=karyo-win32 --gui --registered {context["applicationId"]}\n'
        f'Icon={icon}\nStartupWMClass={window_class}\nTerminal=false\n'
        'Categories=Utility;\nStartupNotify=true\n'
    )
    atomic_bytes(desktop,content.encode('utf-8'))
