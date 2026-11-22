# SPDX-License-Identifier: GPL-3.0-or-later
"""Account-owned short profiles and fixed positional-argument engine plans."""
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import plistlib
import pwd
import re
import shutil
import stat
from .applications import Application
from .errors import LaunchError


MAX_DARLING_PREFIX_BYTES = 79
GRAPHICAL_COMPATIBILITY_VERSION = '10.13.6'
GRAPHICAL_SYSTEM_VERSION = plistlib.dumps({
    'ProductBuildVersion': 'Darling',
    'ProductName': 'macOS',
    'ProductUserVisibleVersion': GRAPHICAL_COMPATIBILITY_VERSION,
    'ProductVersion': GRAPHICAL_COMPATIBILITY_VERSION,
}, fmt=plistlib.FMT_XML)
# Fusion draws Qt Widgets without transient AppKit control views. Runtime 17
# lost the first repaint after a native QMacStyle push button click (fixed by
# Darling patch 0021); Fusion stays the default until native style is accepted
# on both desktops. The launcher-only selector maps to a fixed script; its
# value never reaches Darling. 'native' leaves Qt's default macOS style.
QT_STYLE_VARIABLE = 'KARYO_MACOS_QT_STYLE'
DEFAULT_QT_STYLE = 'fusion'
QT_STYLE_ASSIGNMENTS = {'fusion': 'QT_STYLE_OVERRIDE=Fusion ', 'native': ''}
QT_STYLE_NAMES = {'fusion': 'Fusion', 'native': 'native'}
# Every caller-controlled path/argument remains a positional parameter. A new
# upper-layer file is renamed over the read-only lower plist inside this prefix.
_GRAPHICAL_LAUNCH_TEMPLATE = '''set -eu
umask 077
profile_source=$1
shift
version_path=/System/Library/CoreServices/SystemVersion.plist
profile_temp="${version_path}.karyo-$$"
(set -C; : > "$profile_temp") || exit 1
trap '/bin/rm -f "$profile_temp"' EXIT
/bin/cp "$profile_source" "$profile_temp"
/bin/mv -f "$profile_temp" "$version_path"
trap - EXIT
exec /usr/bin/env QT_MAC_WANTS_LAYER=0 @QT_STYLE@"$@"
'''
GRAPHICAL_LAUNCH_SCRIPTS = {style: _GRAPHICAL_LAUNCH_TEMPLATE.replace('@QT_STYLE@', assignment)
                            for style, assignment in QT_STYLE_ASSIGNMENTS.items()}
GRAPHICAL_LAUNCH_SCRIPT = GRAPHICAL_LAUNCH_SCRIPTS[DEFAULT_QT_STYLE]


@dataclass(frozen=True)
class LaunchPlan:
    application: Application
    argv: list
    environment: dict
    prefix: Path
    log: Path
    session: str
    cwd: Path
    compatibility_profile: Path | None = None
    qt_style: str | None = None
    persistent: bool = True


def normal_user():
    uid, euid, saved_uid = os.getresuid()
    gid, egid, saved_gid = os.getresgid()
    if uid == 0 or uid != euid or uid != saved_uid or gid != egid or gid != saved_gid:
        raise LaunchError('Запускайте macOS-приложения от обычного пользователя, без sudo.')
    return uid, Path(pwd.getpwuid(uid).pw_dir).resolve(strict=True)


def desktop_session(environment):
    # Separate Unix accounts own Home/Work/other desktops. Old slot variables
    # cannot choose a different account's data. Keep a server per X display;
    # Darling's persistent WindowServer must not reconnect to a stale display.
    display = environment.get('DISPLAY', '')
    wayland = environment.get('WAYLAND_DISPLAY', '')
    return 'd' + hashlib.sha256((display + '\0' + wayland).encode()).hexdigest()[:8]


def requested_qt_style(environment):
    """Return the launcher's validated style key; only fixed values are accepted."""
    value = environment.get(QT_STYLE_VARIABLE, DEFAULT_QT_STYLE)
    if value not in QT_STYLE_ASSIGNMENTS:
        raise LaunchError(f'{QT_STYLE_VARIABLE} допускает только значения fusion или native.')
    return value


def engine_environment(environment, prefix):
    clean = {key: value for key, value in environment.items()
             if not key.startswith(('QT_', 'QML', 'QSG_'))
             and key not in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'DPREFIX', QT_STYLE_VARIABLE, 'OPENKARYON_MACOS_QT_STYLE')}
    account = pwd.getpwuid(os.getuid())
    clean.update(DPREFIX=str(prefix), HOME=account.pw_dir, USER=account.pw_name, LOGNAME=account.pw_name)
    # DISPLAY and XAUTHORITY must remain available to Darling's Linux-backed
    # WindowServer. Qt's Linux platform/plugin settings must not reach Cocoa.
    # Mesa's X11 MIT-SHM path needs the display server's SysV IPC namespace.
    # Never inherit this opt-in for a headless launch. Darwin services retain
    # their separate per-application/desktop prefix.
    if clean.get('DISPLAY'):
        clean['DARLING_SHARE_X11_IPC'] = '1'
    else:
        clean.pop('DARLING_SHARE_X11_IPC', None)
    return clean


def open_private_directory(path, uid, create=False):
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise LaunchError('Для профиля нужен абсолютный путь без переходов к родителю.')
    descriptor = os.open(path.anchor, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for name in path.parts[1:]:
            if create:
                try:
                    os.mkdir(name, 0o700, dir_fd=descriptor)
                except FileExistsError:
                    pass
            child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                            dir_fd=descriptor)
            os.close(descriptor)
            descriptor = child
        if os.fstat(descriptor).st_uid != uid:
            raise LaunchError('Каталог профиля не принадлежит текущему пользователю.')
        result, descriptor = descriptor, None
        return result
    finally:
        if descriptor is not None:
            os.close(descriptor)


def private_directory(path, uid):
    descriptor = open_private_directory(path, uid, create=True)
    try:
        os.fchmod(descriptor, 0o700)
    finally:
        os.close(descriptor)


def owned_compatibility_profile(parent, uid):
    name = 'SystemVersion-10.13.6.plist'
    descriptor = open_private_directory(parent, uid)
    temporary = None
    try:
        try:
            existing = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                               dir_fd=descriptor)
        except FileNotFoundError:
            existing = None
        if existing is not None:
            with os.fdopen(existing, 'rb') as stream:
                info = os.fstat(stream.fileno())
                if not stat.S_ISREG(info.st_mode) or info.st_uid != uid or info.st_nlink != 1:
                    raise LaunchError('Файл профиля должен быть обычным файлом текущего пользователя без ссылок.')
                if stream.read(65537) == GRAPHICAL_SYSTEM_VERSION and stat.S_IMODE(info.st_mode) == 0o600:
                    return Path(parent) / name
        # The parent was verified through descriptors. Use an exclusive random
        # sibling and rename only our owned metadata file, never prefix content.
        import uuid
        temporary = '.SystemVersion-' + uuid.uuid4().hex + '.tmp'
        child = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                        0o600, dir_fd=descriptor)
        os.fchmod(child, 0o600)
        with os.fdopen(child, 'wb') as stream:
            stream.write(GRAPHICAL_SYSTEM_VERSION)
            stream.flush()
            os.fsync(stream.fileno())
        os.rename(temporary, name, src_dir_fd=descriptor, dst_dir_fd=descriptor)
        os.fsync(descriptor)
        temporary = None
        return Path(parent) / name
    finally:
        if temporary is not None:
            os.unlink(temporary, dir_fd=descriptor)
        os.close(descriptor)


def mount_filesystem_type(path, mountinfo=Path('/proc/self/mountinfo')):
    """Return the filesystem type of the mount containing an existing path."""
    target = os.path.realpath(path)
    best, best_type = '', None
    for line in mountinfo.read_text(errors='replace').splitlines():
        fields, _, tail = line.partition(' - ')
        parts = fields.split()
        if len(parts) < 5 or not tail:
            continue
        point = re.sub(r'\\([0-7]{3})', lambda match: chr(int(match.group(1), 8)), parts[4])
        if (target == point or target.startswith(point.rstrip('/') + '/')) and len(point) >= len(best):
            best, best_type = point, tail.split()[0]
    return best_type


def prefix_base(home, uid, environment):
    """Choose where Darling prefixes live for this user.

    Darling mounts an overlay with the prefix as its upper directory. A live
    session's home is itself overlayfs, which the kernel rejects as an upper
    directory, so such homes use the private per-login runtime directory.
    Darling replaces /dev/shm in its own mount namespace, so that is no option.
    """
    base = Path(home) / '.karyo-macos'
    if mount_filesystem_type(home) != 'overlay':
        return base
    runtime = environment.get('XDG_RUNTIME_DIR', '')
    message = ('Домашний каталог находится на overlayfs (live-сессия), а Darling не может хранить '
               'профиль на overlayfs. Нужен личный XDG_RUNTIME_DIR на tmpfs.')
    if not runtime or not os.path.isabs(runtime):
        raise LaunchError(message)
    path = Path(runtime)
    try:
        details = path.lstat()
    except OSError as error:
        raise LaunchError(message) from error
    if (not stat.S_ISDIR(details.st_mode) or details.st_uid != uid
            or stat.S_IMODE(details.st_mode) & 0o077 or mount_filesystem_type(path) != 'tmpfs'):
        raise LaunchError(message)
    return path / 'karyo-macos'


def darwin_host_path(path):
    if not path.is_absolute():
        raise LaunchError('Для передачи Darling нужен абсолютный путь приложения или профиля.')
    return '/Volumes/SystemRoot' + str(path)


def prepare_launch(application, arguments, environment=None, create=True):
    environment = dict(os.environ if environment is None else environment)
    uid, home = normal_user()
    style = requested_qt_style(environment)
    engine = shutil.which('darling')
    if create and not engine:
        raise LaunchError('Darling не установлен. Нужен отдельный пакет karyo-darling-runtime.')
    if not os.access(application.executable, os.X_OK):
        raise LaunchError('У приложения нет права исполнения. Разрешите запуск файла (chmod +x).')
    key = 'a' + hashlib.sha256(os.fsencode(application.path)).hexdigest()[:16]
    session = desktop_session(environment)
    base = prefix_base(home, uid, environment)
    parent = base / key / session
    prefix = parent / 'p'
    if len(os.fsencode(prefix)) > MAX_DARLING_PREFIX_BYTES:
        raise LaunchError('Путь профиля Darling слишком длинный: допустимо не более 79 байт UTF-8. Требуется более короткий путь домашнего каталога.')
    if create:
        for directory in (base, parent.parent, parent):
            private_directory(directory, uid)
        if os.path.lexists(prefix):
            descriptor = open_private_directory(prefix, uid)
            os.close(descriptor)
        private_directory(parent / 'logs', uid)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    log = parent / 'logs' / f'{stamp}-{os.getpid()}.log'
    clean = engine_environment(environment, prefix)
    # The account database is authoritative even if HOME/USER or old slot
    # variables were spoofed by a terminal or an inherited launch environment.
    account = pwd.getpwuid(uid)
    clean.update(HOME=str(home), USER=account.pw_name, LOGNAME=account.pw_name)
    profile, qt_style = None, None
    argv = [engine or 'darling', 'exec', str(application.executable), *arguments]
    if clean.get('DISPLAY'):
        profile = owned_compatibility_profile(parent, uid) if create else parent / 'SystemVersion-10.13.6.plist'
        qt_style = QT_STYLE_NAMES[style]
        argv = [engine or 'darling', 'shell', '/bin/bash', '--noprofile', '--norc', '-c',
                GRAPHICAL_LAUNCH_SCRIPTS[style], 'karyo-macos', darwin_host_path(profile),
                darwin_host_path(application.executable), *arguments]
    return LaunchPlan(application, argv, clean, prefix, log, session, Path.cwd(), profile, qt_style, persistent=base == home / '.karyo-macos')


def open_log(plan):
    uid, _ = normal_user()
    parent = open_private_directory(plan.log.parent, uid)
    try:
        descriptor = os.open(plan.log.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC,
                             0o600, dir_fd=parent)
    finally:
        os.close(parent)
    os.fchmod(descriptor, 0o600)
    stream = os.fdopen(descriptor, 'wb')
    header = {'application': str(plan.application.path), 'executable': str(plan.application.executable),
              'argv': plan.argv, 'prefix': str(plan.prefix), 'session': plan.session,
              'display': plan.environment.get('DISPLAY'), 'cwd': str(plan.cwd), 'ownerUid': uid,
              'persistent': plan.persistent}
    if plan.compatibility_profile is not None:
        header['compatibilityProfile'] = {'version': GRAPHICAL_COMPATIBILITY_VERSION,
            'renderer': 'NSWindow raster backing store', 'qtStyle': plan.qt_style,
            'source': str(plan.compatibility_profile),
            'sourceSha256': hashlib.sha256(GRAPHICAL_SYSTEM_VERSION).hexdigest()}
    stream.write((json.dumps(header, ensure_ascii=False) + '\n').encode())
    stream.flush()
    return stream
