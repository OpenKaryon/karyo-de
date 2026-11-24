#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit ordinary Linux DSO thread/signal check for prepared Darling source.

This checks inherited masks and normal child notification, not the complete
Darwin registration path, mldr executable or installed application behavior.
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
    parser.add_argument('--source', type=Path, required=True,
                        help='Prepared src/startup/mldr/elfcalls/native_threads.c')
    parser.add_argument('--output', type=Path, required=True, help='Fresh check directory')
    options = parser.parse_args()
    if os.getuid() == 0 or os.getuid() != os.geteuid():
        raise ValueError('Run this ordinary native check as an unprivileged user')
    data = options.source.read_bytes()
    if len(data) > 64 * 1024:
        raise ValueError('Expected one bounded native thread boundary source')
    compiler = shutil.which('cc')
    if not compiler:
        raise ValueError('A native Linux C compiler is required')
    options.output.mkdir()
    output = options.output.resolve()
    fixtures = Path(__file__).parent / 'fixtures'

    def run(label, argv):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=30)
        (output / (label + '.log')).write_text(result.stdout + result.stderr)
        result.check_returncode()
        return result

    common = [compiler, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2', '-pthread']
    run('library', common + ['-fPIC', '-shared',
        str((fixtures / 'native-thread-factory.c').resolve()),
        '-o', str(output / 'libordinaryfactory.so')])
    checks = {}
    for stage in ['before', 'after']:
        arguments = common + [str((fixtures / 'native-thread-boundary.c').resolve()),
                              '-L' + str(output), '-lordinaryfactory',
                              '-Wl,-rpath,$ORIGIN']
        if stage == 'after':
            arguments += [str(options.source.resolve()), '-ldl',
                          '-Wl,--export-dynamic-symbol=pthread_create']
        binary = output / ('ordinary-' + stage)
        run(stage + '-compile', arguments + ['-o', str(binary)])
        checks[stage] = json.loads(run(stage + '-run', [str(binary)]).stdout)
        for key in ['parent_mask_preserved', 'ordinary_child_exited',
                    'ordinary_SIGCHLD_received']:
            if checks[stage][key] is not True:
                raise ValueError('Ordinary native signal contract failed: ' + key)
    passed = (checks['before']['native_library_worker_masks_both'] is False and
              all(checks['after'].values()))
    record = {'schema': 1, 'scope': 'native DSO worker mask and ordinary child notification',
              'source_sha256': hashlib.sha256(data).hexdigest(), 'checks': checks,
              'all_expected_results': passed,
              'Darwin_registration_or_full_mldr_or_installed_accepted': False,
              'security_or_shared_unit_suites_executed': False,
              'intentional_crash': False}
    (output / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
