# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit ordinary-account prefix copy with offline checks and no replacement."""
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import pwd
import shutil
import stat
import subprocess
import uuid
from .policy import LaunchError, application_id, wine_environment
from .storage import open_directory, secure_directory


FLAGS = os.O_RDONLY if os.name != 'posix' else os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW


def signature(info):
    return (info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns)


def entries(fd, relative=''):
    """Keep each parent descriptor open while its child is inspected/copied."""
    before = os.fstat(fd)
    if before.st_uid != os.getuid():
        raise LaunchError('Каталог исходного префикса принадлежит другому пользователю.')
    yield relative, before, fd, None
    for name in sorted(os.listdir(fd)):
        info = os.stat(name, dir_fd=fd, follow_symlinks=False)
        if info.st_uid != os.getuid():
            raise LaunchError('В исходном префиксе есть объекты другого пользователя: ' + name)
        path = relative + '/' + name if relative else name
        if stat.S_ISDIR(info.st_mode):
            child = os.open(name, FLAGS | os.O_DIRECTORY, dir_fd=fd)
            try:
                if signature(os.fstat(child)) != signature(info):
                    raise LaunchError('Исходный каталог изменился во время проверки.')
                yield from entries(child, path)
            finally:
                os.close(child)
        elif stat.S_ISREG(info.st_mode):
            yield path, info, fd, name
        elif stat.S_ISLNK(info.st_mode):
            target = os.readlink(name, dir_fd=fd)
            if signature(os.stat(name, dir_fd=fd, follow_symlinks=False)) != signature(info):
                raise LaunchError('Исходная ссылка изменилась во время проверки.')
            yield path, info, fd, target
        else:
            raise LaunchError('Специальные файлы нельзя импортировать в Wine-префикс: ' + path)
    if signature(os.fstat(fd)) != signature(before):
        raise LaunchError('Исходный каталог изменился во время проверки.')


def scan(fd):
    return {name: (signature(info), extra) for name, info, _, extra in entries(fd)}


def architecture(fd):
    registry = os.open('system.reg', FLAGS | os.O_NONBLOCK, dir_fd=fd)
    try:
        info = os.fstat(registry)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid():
            raise LaunchError('Некорректный system.reg в исходном префиксе.')
        header = os.read(registry, 8192).splitlines()
        if not header or header[0] != b'WINE REGISTRY Version 2':
            raise LaunchError('Источник не содержит поддерживаемый Wine system.reg.')
        arches = {line[6:] for line in header if line.startswith(b'#arch=')}
        if arches - {b'win32', b'win64'} or len(arches) > 1:
            raise LaunchError('Противоречивая архитектура исходного Wine-префикса.')
        if signature(os.fstat(registry)) != signature(info):
            raise LaunchError('Исходный system.reg изменился во время проверки.')
        return next(iter(arches), b'unknown').decode('ascii'), signature(info)
    finally:
        os.close(registry)


def user_alias(records, selected):
    current = pwd.getpwuid(os.getuid()).pw_name
    if current in ('.', '..') or Path(current).name != current:
        raise LaunchError('Некорректное имя текущего пользователя для Windows-профиля.')
    profiles = sorted(name.split('/')[2] for name, (info, _) in records.items()
                      if name.startswith('drive_c/users/') and name.count('/') == 2
                      and stat.S_ISDIR(info[2]) and name.split('/')[2].lower()
                      not in ('public', 'default', 'default user', 'all users'))
    current_path = 'drive_c/users/' + current
    if current_path in records:
        if selected is not None and selected != current:
            raise LaunchError('Профиль текущего Windows-пользователя уже существует; объединение не выполняется.')
        return profiles, None
    if selected is not None and selected not in profiles:
        raise LaunchError('Указанный Windows-пользователь отсутствует в исходном префиксе.')
    if not selected and len(profiles) > 1:
        return profiles, 'choice-required'
    old = selected or (profiles[0] if profiles else None)
    return profiles, (old, current) if old else None


def analyze(source, home, identifier, selected=None):
    application_id(Path('/'), identifier)
    source = Path(source).expanduser().resolve(strict=True)
    destination = home / '.local/share/karyo/windows/prefixes' / identifier
    if source == destination or source.is_relative_to(destination) or destination.is_relative_to(source):
        raise LaunchError('Исходный и новый префиксы не должны содержать друг друга.')
    fd = open_directory(source)
    try:
        arch, registry_signature = architecture(fd)
        records = scan(fd)
        if records.get('system.reg', (None,))[0] != registry_signature:
            raise LaunchError('Исходный system.reg изменился во время проверки.')
    finally:
        os.close(fd)
    if 'drive_c' not in records or not stat.S_ISDIR(records['drive_c'][0][2]):
        raise LaunchError('В исходном Wine-префиксе отсутствует каталог drive_c.')
    profiles, alias = user_alias(records, selected)
    links = sum(stat.S_ISLNK(row[0][2]) for row in records.values())
    result = {'version': 1, 'source': str(source), 'prefix': str(destination),
              'applicationId': identifier, 'ownerUid': os.getuid(), 'architecture': arch,
              'files': sum(stat.S_ISREG(row[0][2]) for row in records.values()),
              'bytes': sum(row[0][4] for row in records.values() if stat.S_ISREG(row[0][2])),
              'symlinks': links, 'windowsUsers': profiles, 'userAlias': alias,
              'sourceContentsPreserved': True, 'architectureConverted': False}
    return result, records


def wait_idle(source, environment):
    server = shutil.which('wineserver')
    if server is None:
        query = subprocess.run(['dpkg-query', '-L', 'wine64', 'libwine'],
                               capture_output=True, text=True, timeout=10)
        candidates = sorted({path for path in query.stdout.splitlines()
                             if Path(path).name == 'wineserver' and Path(path).is_file()
                             and os.access(path, os.X_OK)})
        if len(candidates) != 1:
            raise LaunchError('Не найден wineserver выбранного Wine. Установите полный native Wine runtime.')
        server = candidates[0]
    env = wine_environment({'prefix': str(source)}, environment)
    try:
        result = subprocess.run([server, '-w'], env=env, stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE, timeout=5)
    except subprocess.TimeoutExpired as error:
        raise LaunchError('Закройте Wine-приложения исходного префикса и повторите импорт.') from error
    if result.returncode:
        raise LaunchError('Не удалось проверить остановку исходного wineserver.')


def mapped_link(source, relative, target):
    link = Path(relative)
    absolute = Path(os.path.normpath(target if os.path.isabs(target)
                                    else str(source / link.parent / target)))
    if absolute.is_relative_to(source):
        return os.path.relpath(absolute.relative_to(source), link.parent)
    # Preserve relative external mappings despite the different destination depth.
    return str(absolute)


def copy_prefix(fd, source, destination, expected):
    fingerprints = []
    for relative, info, parent, extra in entries(fd):
        if (signature(info), extra) != expected.get(relative):
            raise LaunchError('Исходный префикс изменился; импорт отменён.')
        target = destination / relative
        if stat.S_ISDIR(info.st_mode):
            if relative:
                target.mkdir(mode=0o700)
        elif stat.S_ISLNK(info.st_mode):
            mapped = mapped_link(source, relative, extra)
            target.symlink_to(mapped)
            fingerprints.append([relative, 'link', mapped])
        else:
            child = os.open(extra, FLAGS | os.O_NONBLOCK, dir_fd=parent)
            try:
                if signature(os.fstat(child)) != signature(info):
                    raise LaunchError('Исходный файл изменился; импорт отменён.')
                digest = hashlib.sha256()
                with os.fdopen(os.dup(child), 'rb') as incoming, target.open('xb') as outgoing:
                    while True:
                        block = incoming.read(1024 * 1024)
                        if not block:
                            break
                        digest.update(block)
                        outgoing.write(block)
                    outgoing.flush()
                    os.fsync(outgoing.fileno())
                target.chmod(stat.S_IMODE(info.st_mode) & 0o700)
                os.utime(target, ns=(info.st_atime_ns, info.st_mtime_ns), follow_symlinks=False)
                if signature(os.fstat(child)) != signature(info):
                    raise LaunchError('Исходный файл изменился во время копирования.')
                fingerprints.append([relative, 'file', digest.hexdigest()])
            finally:
                os.close(child)
    return hashlib.sha256(json.dumps(fingerprints, ensure_ascii=True).encode()).hexdigest()


def publish(parent, temporary, identifier):
    native = ctypes.CDLL(None, use_errno=True)
    rename = getattr(native, 'renameat2', None)
    if rename is None:
        raise LaunchError('Система не поддерживает атомарный импорт без замены каталога.')
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(parent, os.fsencode(temporary), parent, os.fsencode(identifier), 1) != 0:
        code = ctypes.get_errno()
        if code in (errno.EEXIST, errno.ENOTEMPTY):
            raise LaunchError('Профиль уже существует; импорт не заменяет его данные.')
        raise OSError(code, os.strerror(code))


def import_prefix(source, home, identifier, environment, selected=None):
    result, records = analyze(source, home, identifier, selected)
    if result['userAlias'] == 'choice-required':
        raise LaunchError('В префиксе несколько Windows-пользователей. Укажите --windows-user.')
    source = Path(result['source'])
    destination = Path(result['prefix'])
    if os.path.lexists(destination):
        raise LaunchError('Профиль уже существует; выберите другой --app-id. Данные не заменены.')
    storage = destination.parent.parent
    data_home = Path(environment.get('XDG_DATA_HOME') or home / '.local/share')
    if not data_home.is_absolute():
        raise LaunchError('XDG_DATA_HOME должен быть абсолютным путём.')
    conflicts = (storage / 'applications' / (identifier + '.json'),
                 storage / 'icons' / (identifier + '.png'),
                 data_home / 'applications' / ('karyo-win32-' + identifier + '.desktop'))
    if any(os.path.lexists(path) for path in conflicts):
        raise LaunchError('Этот --app-id уже используется ярлыком приложения; выберите другое имя.')
    wait_idle(source, environment)
    secure_directory(destination.parent)
    parent = open_directory(destination.parent)
    temporary = '.import-' + identifier + '-' + uuid.uuid4().hex
    staging = destination.parent / temporary
    os.mkdir(temporary, 0o700, dir_fd=parent)
    source_fd = None
    try:
        source_fd = open_directory(source)
        digest = copy_prefix(source_fd, source, staging, records)
        if scan(source_fd) != records:
            raise LaunchError('Исходный префикс изменился; импорт отменён.')
        if result['userAlias']:
            old, current = result['userAlias']
            (staging / 'drive_c/users' / current).symlink_to(old, target_is_directory=True)
        # Persist every copied directory entry before making the prefix visible.
        directories = [name for name, (info, _) in records.items() if stat.S_ISDIR(info[2])]
        for relative in sorted(directories, key=lambda name: (bool(name), name.count('/')), reverse=True):
            target = staging / relative
            info = records[relative][0]
            os.utime(target, ns=(info[5], info[5]), follow_symlinks=False)
            directory_fd = open_directory(target)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        wait_idle(source, environment)
        publish(parent, temporary, identifier)
        os.fsync(parent)
        return {**result, 'status': 'imported', 'contentSha256': digest}
    finally:
        if source_fd is not None:
            os.close(source_fd)
        os.close(parent)
        if staging.exists():
            shutil.rmtree(staging)
