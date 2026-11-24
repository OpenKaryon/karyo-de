#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit ordinary native lifecycle check for a prepared Darling source file.

This checks the exact extracted getInitProcess body against real Linux /proc.
It neither builds the complete startup unit nor validates an installed runtime.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Prepared src/startup/darling.c')
    parser.add_argument('--output', type=Path, required=True, help='Fresh ordinary check directory')
    options = parser.parse_args()
    if os.getuid() == 0 or os.getuid() != os.geteuid():
        raise ValueError('Run this ordinary lifecycle check as an unprivileged user')
    data = options.source.read_bytes()
    if len(data) > 1024 * 1024:
        raise ValueError('Expected a single bounded startup source file')
    text = data.decode('utf-8')
    signature = 'pid_t getInitProcess()\n'
    if text.count(signature) != 1:
        raise ValueError('Expected one complete getInitProcess definition')
    start = text.index(signature)
    end = text.index('\nvoid checkPrefixOwner()', start)
    body = text[start:end].encode()
    compiler = shutil.which('cc')
    if not compiler:
        raise ValueError('A native Linux C compiler is required')
    options.output.mkdir()
    include = options.output / 'get-init-process.inc'
    include.write_bytes(body)
    fixture = Path(__file__).parent / 'fixtures/startup-server-lifecycle.c'
    binary = options.output / 'ordinary-startup-lifecycle'
    build = subprocess.run([compiler, '-std=gnu11', '-Wall', '-Wextra',
                            '-I', str(options.output.resolve()), str(fixture.resolve()),
                            '-o', str(binary.resolve())], capture_output=True, text=True, timeout=30)
    (options.output / 'compile.log').write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary.resolve())], capture_output=True, text=True, timeout=15)
    (options.output / 'run.log').write_text(run.stdout + run.stderr)
    record = {'schema': 1, 'scope': 'exact extracted function, real ordinary same-user child lifecycle',
              'source_sha256': hashlib.sha256(data).hexdigest(),
              'function_sha256': hashlib.sha256(body).hexdigest(),
              'status': run.returncode, 'checks': json.loads(run.stdout),
              'full_startup_unit_or_installed_runtime_accepted': False,
              'security_or_shared_unit_suites_executed': False}
    (options.output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record))
    return run.returncode


if __name__ == '__main__':
    raise SystemExit(main())
