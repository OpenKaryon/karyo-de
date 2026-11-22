# SPDX-License-Identifier: GPL-3.0-or-later
"""Public nonprivileged CLI; engine installation never implies compatibility."""
import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
from .applications import inspect_application
from .errors import LaunchError
from .profiles import normal_user, prepare_launch
from .process import wait_launch


def discard_inherited_capabilities():
    # Darling's separately installed engine owns its privilege mechanism. Clear
    # ambient/current capabilities without setting NoNewPrivs, which would disable
    # that engine's audited setuid bootstrap. This wrapper is never privileged.
    native = ctypes.CDLL(None, use_errno=True)
    class Header(ctypes.Structure):
        _fields_ = [('version', ctypes.c_uint32), ('pid', ctypes.c_int)]
    class Data(ctypes.Structure):
        _fields_ = [('effective', ctypes.c_uint32), ('permitted', ctypes.c_uint32), ('inheritable', ctypes.c_uint32)]
    header, data = Header(0x20080522, 0), (Data * 2)()
    if native.prctl(47, 4, 0, 0, 0) != 0 or native.capset(ctypes.byref(header), data) != 0:
        raise LaunchError('Не удалось сбросить унаследованные права перед запуском Darling.')


def runtime_status():
    engine = shutil.which('darling')
    version = None
    if engine and shutil.which('dpkg-query'):
        try:
            query = subprocess.run(['dpkg-query', '-W', '-f=${Status}\t${Version}', 'karyo-darling-runtime'],
                                   capture_output=True, text=True, timeout=5)
            if query.returncode == 0 and query.stdout.startswith('install ok installed\t'):
                version = query.stdout.partition('\t')[2].strip()
        except (OSError, subprocess.TimeoutExpired):
            pass
    return {'schema': 1, 'experimental': True, 'installed': engine is not None,
            'engine': engine, 'package': 'karyo-darling-runtime', 'version': version,
            'supportedInputs': ['x86_64 Mach-O executable', '.app with x86_64 executable'],
            'applicationCompatibility': 'not implied by engine installation'}


def inspection(plan):
    uid, _ = normal_user()
    return {'schema': 1, 'experimental': True, 'application': str(plan.application.path),
            'executable': str(plan.application.executable), 'name': plan.application.name,
            'bundle': plan.application.bundle, 'prefix': str(plan.prefix), 'ownerUid': uid,
            'session': plan.session, 'log': str(plan.log), 'qtStyle': plan.qt_style,
            'persistent': plan.persistent,
            'runtime': runtime_status(), 'prefixCreated': False}


def main(argv=None, picker_path='/usr/bin/karyo-macos-picker'):
    parser = argparse.ArgumentParser(description='Экспериментальный запуск macOS x86_64-приложений через Darling.')
    parser.add_argument('--gui', action='store_true', help='Открыть окно выбора приложения Karyo.')
    parser.add_argument('--status', action='store_true', help='Наличие движка; совместимость проверяется отдельно.')
    parser.add_argument('--json', action='store_true', help='Машиночитаемый статус.')
    parser.add_argument('--inspect', action='store_true', help='Проверить приложение и путь профиля без записи и запуска.')
    parser.add_argument('application', nargs='?')
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    options = parser.parse_args(argv)
    try:
        if options.status:
            if options.inspect or options.application or options.gui:
                raise LaunchError('--status нельзя сочетать с запуском приложения.')
            result = runtime_status()
            print(json.dumps(result, ensure_ascii=False) if options.json else
                  ('Darling установлен; совместимость приложений проверяется отдельно.' if result['installed'] else 'Darling не установлен.'))
            return 0 if result['installed'] else 1
        normal_user()
        if options.gui or (options.application is None and not options.inspect):
            if options.inspect:
                raise LaunchError('--gui нельзя сочетать с --inspect.')
            if not os.path.isfile(picker_path) or not os.access(picker_path, os.X_OK):
                raise LaunchError('Для окна выбора установите необязательный пакет karyo-macos.')
            arguments = options.arguments[1:] if options.arguments[:1] == ['--'] else options.arguments
            discard_inherited_capabilities()
            os.execv(picker_path, [picker_path, '--', *([options.application, *arguments] if options.application else [])])
        if not options.application:
            raise LaunchError('Укажите .app bundle или исполняемый Mach-O файл x86_64.')
        application = inspect_application(options.application)
        arguments = options.arguments[1:] if options.arguments[:1] == ['--'] else options.arguments
        plan = prepare_launch(application, arguments, create=not options.inspect)
        if options.inspect:
            print(json.dumps(inspection(plan), ensure_ascii=False, indent=2))
            return 0
        discard_inherited_capabilities()
        return wait_launch(plan)
    except (LaunchError, OSError, subprocess.TimeoutExpired, ValueError) as error:
        print(f'Karyo macOS: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
