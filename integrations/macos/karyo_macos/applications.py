# SPDX-License-Identifier: GPL-3.0-or-later
"""Nonexecuting Intel Mach-O and .app inspection, adapted from the prototype."""
from dataclasses import dataclass
import os
from pathlib import Path
import plistlib
import stat
import struct
from .errors import LaunchError


@dataclass(frozen=True)
class Application:
    path: Path
    executable: Path
    name: str
    bundle: bool


def _thin_macho(header):
    if len(header) < 32 or header[:4] != b'\xcf\xfa\xed\xfe':
        return False
    _, cpu, _, kind = struct.unpack_from('<4I', header)
    return cpu == 0x1000007 and kind == 2


def validate_macho(path):
    """Require a thin Intel 64-bit executable or a valid Intel slice of fat Mach-O."""
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_CLOEXEC)
    with os.fdopen(descriptor, 'rb') as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise LaunchError('Исполняемый файл не является обычным файлом.')
        header = stream.read(32)
        if _thin_macho(header):
            return
        if header[:4] not in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'):
            raise LaunchError('Нужен исполняемый Mach-O для macOS x86_64. ARM, ELF и Windows PE здесь не поддерживаются.')
        if len(header) < 8:
            raise LaunchError('Обрезан заголовок universal Mach-O.')
        count = struct.unpack_from('>I', header, 4)[0]
        if not 1 <= count <= 64:
            raise LaunchError('Повреждён заголовок universal Mach-O.')
        fat64 = header[:4] == b'\xca\xfe\xba\xbf'
        entry_size = 32 if fat64 else 20
        stream.seek(8)
        entries = stream.read(count * entry_size)
        file_size = os.fstat(stream.fileno()).st_size
        if len(entries) != count * entry_size:
            raise LaunchError('Обрезана таблица universal Mach-O.')
        for index in range(count):
            entry = entries[index * entry_size:(index + 1) * entry_size]
            if fat64:
                cpu, _, offset, size, _, _ = struct.unpack('>IIQQII', entry)
            else:
                cpu, _, offset, size, _ = struct.unpack('>IIIII', entry)
            if cpu != 0x1000007:
                continue
            if offset < 8 + count * entry_size or size < 32 or offset + size > file_size:
                raise LaunchError('Неверные границы x86_64-части universal Mach-O.')
            stream.seek(offset)
            if _thin_macho(stream.read(32)):
                return
        raise LaunchError('В universal Mach-O нет исполняемой x86_64-части.')


def inspect_application(value):
    try:
        path = Path(value).expanduser().resolve(strict=True)
        if path.suffix.lower() in ('.pkg', '.dmg'):
            raise LaunchError('Установщики .pkg и образы .dmg пока не поддерживаются. Выберите .app или Mach-O-файл.')
        if path.is_dir():
            if path.suffix.lower() != '.app':
                raise LaunchError('Выбранная папка не является приложением .app.')
            metadata = path / 'Contents' / 'Info.plist'
            descriptor = os.open(metadata, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW | os.O_CLOEXEC)
            with os.fdopen(descriptor, 'rb') as stream:
                details = os.fstat(stream.fileno())
                if not stat.S_ISREG(details.st_mode) or details.st_size > 1024 * 1024:
                    raise LaunchError('Info.plist должен быть обычным файлом не более 1 МиБ.')
                info = plistlib.loads(stream.read(1024 * 1024 + 1))
            if not isinstance(info, dict):
                raise LaunchError('Info.plist должен содержать словарь метаданных приложения.')
            name = info.get('CFBundleExecutable')
            if not isinstance(name, str) or not name or name in ('.', '..') or '/' in name or '\\' in name or '\0' in name:
                raise LaunchError('В Info.plist отсутствует безопасное имя CFBundleExecutable.')
            if info.get('CFBundlePackageType', 'APPL') != 'APPL':
                raise LaunchError('Bundle не имеет тип приложения APPL.')
            executable = (path / 'Contents' / 'MacOS' / name).resolve(strict=True)
            if not executable.is_relative_to(path):
                raise LaunchError('Исполняемый файл выходит за пределы .app bundle.')
            title = info.get('CFBundleDisplayName') or info.get('CFBundleName') or path.stem
            app = Application(path, executable, str(title), True)
        else:
            app = Application(path, path, path.name, False)
        if not stat.S_ISREG(app.executable.stat().st_mode):
            raise LaunchError('Исполняемый файл не является обычным файлом.')
        validate_macho(app.executable)
        return app
    except LaunchError:
        raise
    except (OSError, ValueError, plistlib.InvalidFileException, struct.error) as error:
        raise LaunchError(f'Не удалось прочитать приложение: {error}') from error
