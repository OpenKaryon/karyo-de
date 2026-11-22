# SPDX-License-Identifier: GPL-3.0-or-later
"""Stream the direct engine child without waiting for its persistent daemon."""
import os
import selectors
import subprocess
import sys
import time
from .errors import LaunchError
from .profiles import open_log


def stream_output(process, log, output):
    """Stream bytes until the direct child exits, even if its daemon keeps the pipe."""
    descriptor = process.stdout.fileno()
    os.set_blocking(descriptor, False)
    exited_at = None
    last_data_at = None
    with selectors.DefaultSelector() as selector:
        selector.register(descriptor, selectors.EVENT_READ)
        while True:
            now = time.monotonic()
            if process.poll() is not None and exited_at is None:
                exited_at = last_data_at = now
            timeout = 0.05
            if exited_at is not None:
                # Darling's persistent server may inherit stdout. Drain queued
                # diagnostics after the launcher exits; waiting for pipe EOF
                # would wait for that server instead of for the application.
                remaining = min(0.5 - (now - exited_at), 0.1 - (now - last_data_at))
                if remaining <= 0:
                    return
                timeout = min(timeout, remaining)
            if not selector.select(timeout):
                continue
            try:
                chunk = os.read(descriptor, 8192)
            except BlockingIOError:
                continue
            if not chunk:
                return
            log.write(chunk)
            log.flush()
            output.write(chunk)
            output.flush()
            if exited_at is not None:
                last_data_at = time.monotonic()


def wait_launch(plan, output=None):
    output = sys.stdout.buffer if output is None else output
    print(f'Karyo macOS: журнал {plan.log}', file=sys.stderr)
    try:
        with open_log(plan) as log:
            try:
                process = subprocess.Popen(plan.argv, env=plan.environment, cwd=plan.cwd,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            except OSError as error:
                log.write(f'\nDarling launch error: {error}\n'.encode())
                raise
            stream_output(process, log, output)
            code = process.wait()
            process.stdout.close()
            log.write(f'\nKaryo exit code: {code}\n'.encode())
        if code:
            print(f'macOS-приложение завершилось с кодом {code}. Журнал: {plan.log}', file=sys.stderr)
        return code if code >= 0 else 128 - code
    except OSError as error:
        raise LaunchError(f'Не удалось запустить Darling: {error}. Журнал: {plan.log}') from error
