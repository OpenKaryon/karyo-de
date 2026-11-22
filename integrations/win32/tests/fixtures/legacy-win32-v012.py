# SPDX-License-Identifier: GPL-3.0-or-later
# Frozen compatibility fixture; never installed as a runtime helper.
#!/usr/bin/python3
"""Desktop-facing Wine launcher; never executes Windows code as root."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time

VERSION = '0.1.2'
APP_ID_RE = re.compile(r'^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$')
_qt_application = None


class LaunchError(RuntimeError):
    def __init__(self, message, returncode=1):
        super().__init__(message)
        self.returncode = min(255, 128 - returncode) if returncode < 0 else min(255, returncode or 1)


def user_home():
    if os.getuid() == 0 or os.geteuid() == 0:
        raise LaunchError('Приложения Windows нельзя запускать от root. Войдите обычным пользователем.')
    return Path(pwd.getpwuid(os.getuid()).pw_dir)


def session_key(environment):
    slot = environment.get('OPENKARYON_SESSION_SLOT', '')
    if slot:
        if slot not in ('1', '2'):
            raise LaunchError('Некорректный номер сессии OpenKaryon.')
        return 'session-' + slot
    return 'desktop'


def application_id(path, requested=None):
    if requested is not None:
        if not APP_ID_RE.fullmatch(requested):
            raise LaunchError('Идентификатор приложения: 1–80 букв, цифр, точек, подчёркиваний или дефисов.')
        return requested
    return 'app-' + hashlib.sha256(os.fsencode(path)).hexdigest()[:24]


def validate_application(filename):
    try:
        path = Path(filename).expanduser().resolve(strict=True)
        if not path.is_file():
            raise LaunchError('Выберите файл приложения EXE или установщика MSI.')
        with path.open('rb') as stream:
            header = stream.read(64)
            if path.suffix.lower() == '.msi':
                if header[:8] != b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1':
                    raise LaunchError('Файл не является пакетом Windows Installer (MSI).')
                return path, 'msi'
            if path.suffix.lower() != '.exe' or header[:2] != b'MZ' or len(header) < 64:
                raise LaunchError('Поддерживаются Windows PE-приложения EXE и установщики MSI.')
            pe_offset = struct.unpack_from('<I', header, 60)[0]
            if pe_offset < 64 or pe_offset > path.stat().st_size - 4:
                raise LaunchError('Некорректный заголовок Windows PE.')
            stream.seek(pe_offset)
            if stream.read(4) != b'PE\x00\x00':
                raise LaunchError('Файл EXE не содержит заголовок Windows PE.')
        return path, 'exe'
    except (OSError, ValueError) as error:
        raise LaunchError(f'Не удалось открыть приложение: {error}') from error


def secure_directory(path):
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid():
        raise LaunchError(f'Каталог приложения не принадлежит текущему пользователю: {path}')
    path.chmod(0o700)


def write_json(path, data):
    # Prefixes and records live in private user-owned directories. Replace, never
    # follow an existing record symlink, and keep concurrent launches independent.
    temporary = path.with_name(path.name + f'.{os.getpid()}.{time.monotonic_ns()}.tmp')
    try:
        with temporary.open('x', encoding='utf-8') as stream:
            json.dump(data, stream, ensure_ascii=False, indent=2)
            stream.write('\n')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def launch_context(path, kind, home, environment, requested=None):
    slot = session_key(environment)
    storage = home / '.local/share/openkaryon/windows'
    inherited = None
    try:
        relative = path.relative_to(storage / 'prefixes')
        parts = relative.parts
        if (len(parts) >= 4 and APP_ID_RE.fullmatch(parts[0])
                and parts[1] in ('desktop', 'session-1', 'session-2')
                and parts[2] == 'drive_c'):
            inherited = (parts[0], parts[1], Path(*parts[2:]))
    except ValueError:
        pass
    identifier = application_id(path, requested if requested is not None else (inherited[0] if inherited else None))
    if inherited and identifier == inherited[0] and slot != inherited[1]:
        candidate = storage / 'prefixes' / identifier / slot / inherited[2]
        try:
            path, kind = validate_application(candidate)
        except LaunchError as error:
            raise LaunchError('Приложение установлено в другой сессии. '
                              'Запустите его установщик в текущей сессии OpenKaryon.\n'
                              f'Профиль приложения: {identifier}') from error
    return {
        'version': 1, 'provider': 'wine', 'applicationId': identifier,
        'file': str(path), 'kind': kind, 'session': slot,
        'prefix': str(storage / 'prefixes' / identifier / slot),
        'storage': str(storage),
    }


def wine_environment(context, environment):
    result = environment.copy()
    # The Qt inside a Windows application must select qwindows, not the Linux
    # wayland plugin inherited from LXQt. Display and private D-Bus remain intact.
    for name in (
        'WINEPREFIX', 'WINEARCH', 'WINESERVER', 'WINELOADER', 'WINEDLLPATH',
        'QT_QPA_PLATFORM', 'QT_QPA_PLATFORMTHEME', 'QT_PLUGIN_PATH',
        'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QUICK_BACKEND', 'QML_IMPORT_PATH',
        'QML2_IMPORT_PATH', 'LD_LIBRARY_PATH', 'LD_PRELOAD',
    ):
        result.pop(name, None)
    result.update(WINEPREFIX=context['prefix'], WINEARCH='win64')
    # Wine-generated shortcuts call Wine directly and can reuse the wrong session.
    # Our registered menu entries resolve the current session on every launch.
    overrides = result.get('WINEDLLOVERRIDES', '').strip(';')
    result['WINEDLLOVERRIDES'] = (overrides + ';' if overrides else '') + 'winemenubuilder.exe=d'
    return result


def executable(name):
    value = shutil.which(name)
    if not value:
        raise LaunchError(f'Компонент {name} не установлен. Установите пакет openkaryon-win32.')
    return value


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
        'version': 1, 'applicationId': context['applicationId'], 'file': context['file'],
    })
    data_home = Path(environment.get('XDG_DATA_HOME') or home / '.local/share')
    if not data_home.is_absolute():
        raise LaunchError('XDG_DATA_HOME должен быть абсолютным путём.')
    menu = data_home / 'applications'
    menu.mkdir(parents=True, exist_ok=True)
    executable_path = Path(context['file'])
    name = desktop_escape(executable_path.stem)
    window_class = desktop_escape(executable_path.name.lower())
    icons = Path(context['storage']) / 'icons'
    secure_directory(icons)
    icon_path = icons / (context['applicationId'] + '.png')
    icon = desktop_escape(str(icon_path)) if extract_icon(str(executable_path), icon_path) else 'application-x-ms-dos-executable'
    desktop = menu / ('openkaryon-win32-' + context['applicationId'] + '.desktop')
    content = (
        '[Desktop Entry]\nType=Application\n'
        f'Name={name} (Windows)\n'
        f'Exec=openkaryon-win32 --gui --registered {context["applicationId"]}\n'
        f'Icon={icon}\nStartupWMClass={window_class}\nTerminal=false\n'
        'Categories=Utility;\nStartupNotify=true\n'
    )
    temporary = desktop.with_name(desktop.name + f'.{os.getpid()}.{time.monotonic_ns()}.tmp')
    try:
        temporary.write_text(content, encoding='utf-8')
        temporary.replace(desktop)
    finally:
        temporary.unlink(missing_ok=True)


def registered_application(identifier, home):
    application_id(Path('/'), identifier)
    path = home / '.local/share/openkaryon/windows/applications' / (identifier + '.json')
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
        if (data.get('version') != 1 or data.get('applicationId') != identifier
                or not isinstance(data.get('file'), str)):
            raise ValueError('invalid record')
        return data['file']
    except (OSError, ValueError, AttributeError) as error:
        raise LaunchError(f'Не удалось прочитать запись приложения {identifier}: {error}') from error


def launch(context, arguments, environment, home, register=True):
    wine = executable('wine')
    winepath = executable('winepath') if context['kind'] == 'msi' else None
    prefix = Path(context['prefix'])
    storage = Path(context['storage'])
    for directory in (storage, storage / 'prefixes', prefix.parent, prefix, storage / 'logs'):
        secure_directory(directory)
    env = wine_environment(context, environment)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    token = f'{stamp}-{os.getpid()}-{context["applicationId"]}-{context["session"]}'
    log_path = storage / 'logs' / (token + '.log')
    result_path = storage / 'logs' / (token + '.json')
    record = {**context, 'startedAt': stamp, 'launcherPid': os.getpid(),
              'log': str(log_path), 'returnCode': None}
    write_json(result_path, record)
    try:
        with log_path.open('x', encoding='utf-8') as log:
            if context['kind'] == 'msi':
                converted = subprocess.run(
                    [winepath, '--windows', context['file']], env=env,
                    cwd=str(Path(context['file']).parent), stdout=subprocess.PIPE,
                    stderr=log, text=True, check=False,
                )
                windows_path = converted.stdout.rstrip('\r\n')
                if converted.returncode or not windows_path or '\n' in windows_path:
                    raise LaunchError(f'Wine не смог преобразовать путь MSI. Журнал: {log_path}')
                command = [wine, 'msiexec', '/i', windows_path, *arguments]
            else:
                command = [wine, context['file'], *arguments]
            if register and context['kind'] == 'exe':
                register_application(context, environment, home)
            child = subprocess.Popen(
                command, env=env, cwd=str(Path(context['file']).parent),
                stdout=log, stderr=subprocess.STDOUT,
            )
            record['processId'] = child.pid
            write_json(result_path, record)
            returncode = child.wait()
            record['returnCode'] = returncode
            record['finishedAt'] = datetime.now(timezone.utc).isoformat()
            write_json(result_path, record)
    except (OSError, LaunchError) as error:
        record['error'] = str(error)
        record['finishedAt'] = datetime.now(timezone.utc).isoformat()
        write_json(result_path, record)
        raise LaunchError(f'{error}\nЖурнал: {log_path}') from error
    if returncode:
        raise LaunchError(f'Приложение Windows завершилось с кодом {returncode}.\nЖурнал: {log_path}', returncode)
    return returncode


def qt_application():
    global _qt_application
    if _qt_application is None:
        from PyQt6.QtWidgets import QApplication
        _qt_application = QApplication.instance() or QApplication([sys.argv[0]])
        _qt_application.setApplicationName('Приложения Windows — OpenKaryon')
    return _qt_application


def choose_application():
    qt_application()
    from PyQt6.QtWidgets import QFileDialog
    filename, _ = QFileDialog.getOpenFileName(
        None, 'Запустить приложение Windows', str(Path.home()),
        'Приложения и установщики Windows (*.exe *.EXE *.msi *.MSI)',
    )
    return filename


def show_error(message, graphical):
    print(f'openkaryon-win32: {message}', file=sys.stderr)
    if graphical and os.getuid() != 0 and os.geteuid() != 0:
        try:
            qt_application()
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.critical(None, 'Не удалось запустить приложение Windows', message)
        except (ImportError, RuntimeError) as error:
            print(f'Не удалось показать сообщение: {error}', file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description='Запуск Windows EXE и MSI через Wine от текущего пользователя.')
    parser.add_argument('--version', action='version', version=VERSION)
    parser.add_argument('--gui', action='store_true', help='Выбор файла и сообщения об ошибках в окнах Qt.')
    parser.add_argument('--app-id', help='Общий профиль для установщика и установленного приложения.')
    parser.add_argument('--registered', metavar='ID', help='Запустить сохранённый пункт меню.')
    parser.add_argument('--inspect', action='store_true', help='Показать JSON с выбранным prefix без запуска Wine.')
    parser.add_argument('--no-register', action='store_true', help='Не добавлять EXE в меню приложений.')
    parser.add_argument('file', nargs='?')
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    try:
        home = user_home()
        os.umask(0o077)
        if args.registered:
            if args.file or args.app_id:
                raise LaunchError('--registered нельзя сочетать с файлом или --app-id.')
            args.file = registered_application(args.registered, home)
            args.app_id = args.registered
        if not args.file:
            if args.inspect:
                raise LaunchError('Укажите файл приложения для --inspect.')
            args.gui = True
            args.file = choose_application()
            if not args.file:
                return 0
        path, kind = validate_application(args.file)
        context = launch_context(path, kind, home, os.environ, args.app_id)
        if args.inspect:
            print(json.dumps(context, ensure_ascii=False, indent=2))
            return 0
        return launch(context, args.arguments, os.environ, home, not args.no_register)
    except (LaunchError, OSError, ImportError) as error:
        show_error(str(error), args.gui)
        return error.returncode if isinstance(error, LaunchError) else 1
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
