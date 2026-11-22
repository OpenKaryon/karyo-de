# SPDX-License-Identifier: GPL-3.0-or-later
"""Optional desktop-facing Wine launcher; never executes Windows code as root."""
import argparse,json,os,subprocess,sys
from datetime import datetime,timezone
from pathlib import Path
from .policy import LaunchError,VERSION,drop_privileges,executable,launch_context,user_home,validate_application,wine_environment
from .storage import registered_application,secure_directory,write_json
from .desktop import register_application

_qt_application=None

def launch(context, arguments, environment, home, register=True):
    wine = executable('wine')
    winepath = executable('winepath') if context['kind'] == 'msi' else None
    prefix = Path(context['prefix'])
    storage = Path(context['storage'])
    for directory in (storage, storage / 'prefixes', prefix.parent, prefix, storage / 'logs'):
        secure_directory(directory)
    env = wine_environment(context, environment)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    token = f'{stamp}-{os.getpid()}-{context["applicationId"]}'
    log_path = storage / 'logs' / (token + '.log')
    result_path = storage / 'logs' / (token + '.json')
    record = {**context, 'startedAt': stamp, 'launcherPid': os.getpid(),
              'log': str(log_path), 'returnCode': None}
    write_json(result_path, record)
    try:
        with os.fdopen(os.open(log_path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600),'w',encoding='utf-8') as log:
            if context['kind'] == 'msi':
                converted = subprocess.run(
                    [winepath, '--windows', context['file']], env=env,
                    cwd=str(Path(context['file']).parent), stdout=subprocess.PIPE,
                    stderr=log, text=True, check=False, timeout=120,
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
    except (OSError, subprocess.SubprocessError, LaunchError) as error:
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
        from PyQt6.QtGui import QIcon
        from PyQt6.QtWidgets import QApplication,QStyle
        _qt_application = QApplication.instance() or QApplication([sys.argv[0]])
        _qt_application.setApplicationName('Приложения Windows — Karyo')
        _qt_application.setDesktopFileName('karyo-win32')
        _qt_application.setWindowIcon(QIcon.fromTheme(
            'application-x-ms-dos-executable',
            _qt_application.style().standardIcon(QStyle.StandardPixmap.SP_ComputerIcon)))
    return _qt_application
def choose_application(home):
    qt_application()
    from PyQt6.QtWidgets import QFileDialog
    filename, _ = QFileDialog.getOpenFileName(
        None, 'Запустить приложение Windows', str(home),
        'Приложения и установщики Windows (*.exe *.EXE *.msi *.MSI)',
    )
    return filename
def show_error(message, graphical):
    print(f'karyo-win32: {message}', file=sys.stderr)
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
        if not args.inspect:drop_privileges()
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
            args.file = choose_application(home)
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
