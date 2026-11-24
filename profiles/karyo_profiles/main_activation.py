# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit, reversible LightDM integration; package installation stays inert."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

from .credentials import LocalAccounts
from .dispatcher import unique_object
from .journal_network import offline
from .logind import Sessions
from .main_dispatcher import POLICY
from .model import account_name, root_file
from .networks import NetworkCommand
from .runtime_policy import enabled, forwarding_ready
from .services import host_controller
from .storage import ConfigStore, atomic_write, trusted_directory

STATE = Path('/var/lib/karyo/main_login')
PAM = Path('/etc/pam.d')
NAMES = ('lightdm', 'lightdm-autologin')


def patched(original, module):
    anchor = '@include common-session\n'
    if original.count(anchor) != 1 or 'pam_karyo_network.so' in original:
        raise ValueError('The LightDM session stack requires administrator review')
    if any('pam_systemd.so' in line for line in original.splitlines() if not line.lstrip().startswith('#')):
        raise ValueError('The LightDM stack already starts a user manager outside common-session')
    before = 'session requisite ' + str(module) + ' prepare desktop=karyo\n'
    after = 'session requisite ' + str(module) + ' attach desktop=karyo\n'
    return original.replace(anchor, before + anchor + after)


def private_json(directory, name):
    text = root_file(name, dir_fd=directory)
    info = os.stat(name, dir_fd=directory, follow_symlinks=False)
    if stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1:
        raise PermissionError('Main-login state must be a private root-owned file')
    return json.loads(text, object_pairs_hook=unique_object)


def save(directory, value):
    data = (json.dumps(value, ensure_ascii=True, sort_keys=True, indent=2) + '\n').encode()
    atomic_write(directory, 'state.json', data, mode=0o600)


@contextmanager
def state_directory(path):
    parent = trusted_directory(path.parent, create=True)
    try:
        try:os.mkdir(path.name, 0o700, dir_fd=parent)
        except FileExistsError:pass
    finally:os.close(parent)
    directory = trusted_directory(path)
    try:
        if stat.S_IMODE(os.fstat(directory).st_mode) != 0o700:
            raise PermissionError('Main-login backup directory must have mode 0700')
        if set(os.listdir(directory)) - {'state.json'}:
            raise ValueError('Unknown main-login backup objects require review')
        yield directory
    finally:os.close(directory)


def validate(value, module):
    if (not isinstance(value, dict) or set(value) != {'version', 'phase', 'module', 'users', 'files', 'common_sha256'}
            or type(value['version']) is not int or value['version'] != 1 or value['module'] != str(module)
            or value['phase'] not in ('installing', 'active', 'removing', 'inactive')
            or not isinstance(value['users'], list) or not 1 <= len(value['users']) <= 16
            or len(set(account_name(name) for name in value['users'])) != len(value['users'])
            or not isinstance(value['files'], dict) or set(value['files']) != set(NAMES)
            or not isinstance(value['common_sha256'], str) or len(value['common_sha256']) != 64):
        raise ValueError('Invalid main-login activation receipt')
    for entry in value['files'].values():
        if (not isinstance(entry, dict) or set(entry) != {'original', 'installed'}
                or not isinstance(entry['original'], str)
                or entry['installed'] != patched(entry['original'], module)):
            raise ValueError('Invalid main-login PAM backup')


def module_ready(module):
    if (not module.is_absolute() or module.name != 'pam_karyo_network.so'
            or any(part.isspace() for part in str(module)) or '..' in module.parts):
        raise ValueError('Invalid installed PAM module path')
    directory = trusted_directory(module.parent)
    try:
        descriptor = os.open(module.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=directory)
        try:
            info = os.fstat(descriptor)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022
                    or info.st_nlink != 1 or os.read(descriptor, 4) != b'\x7fELF'):
                raise ValueError('The installed main-login PAM module requires review')
        finally:os.close(descriptor)
    finally:os.close(directory)


def policy_text(users):
    return json.dumps({'version': 1, 'users': users}, sort_keys=True, separators=(',', ':')) + '\n'


def activate(operation, users, module, *, store=None, accounts=None, sessions=None,
             command=None, pam=PAM, state=STATE, policy=POLICY):
    host_controller()
    if operation not in ('enable', 'disable'):raise ValueError('Unsupported main-login operation')
    store = ConfigStore() if store is None else store
    accounts = LocalAccounts() if accounts is None else accounts
    sessions = Sessions() if sessions is None else sessions
    command = NetworkCommand() if command is None else command
    module = Path(module)
    selected = tuple(account_name(name) for name in users)
    if operation == 'enable' and (not 1 <= len(selected) <= 16 or len(set(selected)) != len(selected)):
        raise ValueError('Select one to sixteen distinct main accounts')
    if operation == 'disable' and selected:raise ValueError('Disable uses the captured main accounts')
    # Editing a live display-manager stack would race a PAM prepare which has
    # already returned IGNORE. Maintenance is explicit; this tool stops no user.
    if command.run('/usr/bin/systemctl', 'show', 'lightdm.service', '--value', '--property=ActiveState').strip() != 'inactive':
        raise RuntimeError('Stop LightDM before changing main-login integration')
    with store.locked(), state_directory(state) as backup:
        try:value = private_json(backup, 'state.json')
        except FileNotFoundError:value = None
        if value is not None:validate(value, module)
        if value is None and operation == 'disable':return {'main_login': 'unchanged'}
        if value is not None:
            if operation == 'enable' and tuple(value['users']) != selected:
                if value['phase'] != 'inactive':
                    raise ValueError('Disable the previous main-account selection before replacing it')
                value = dict(value, users=list(selected))
            selected = tuple(value['users'])
        ownership = store.ownership()
        for name in selected:
            if ownership.is_child(name):raise PermissionError('Only a main account may use the LightDM entry')
            offline(accounts.account(name), accounts, sessions)
        pam_directory = trusted_directory(pam)
        policy_directory = trusted_directory(policy.parent, create=True)
        try:
            originals = {}
            for name in NAMES:
                originals[name] = root_file(name, dir_fd=pam_directory)
                info = os.stat(name, dir_fd=pam_directory, follow_symlinks=False)
                if stat.S_IMODE(info.st_mode) != 0o644 or info.st_nlink != 1:
                    raise ValueError('Unsupported LightDM PAM file mode')
            common = root_file('common-session', dir_fd=pam_directory)
            common_hash = hashlib.sha256(common.encode()).hexdigest()
            if value is None:
                if os.path.lexists(policy):raise ValueError('An uncaptured main-login policy already exists')
                value = {'version': 1, 'phase': 'installing', 'module': str(module), 'users': list(selected),
                         'files': {name: {'original': text, 'installed': patched(text, module)}
                                   for name, text in originals.items()}, 'common_sha256': common_hash}
            if operation == 'enable' and value['common_sha256'] != common_hash:
                raise ValueError('The captured common-session stack changed; review main-login ordering')
            for name, text in originals.items():
                if text not in value['files'][name].values():
                    raise ValueError('LightDM PAM policy changed outside this activation')
            expected = policy_text(list(selected))
            try:
                current = root_file(policy.name, dir_fd=policy_directory)
                if current != expected:raise ValueError('Main-login policy changed outside this activation')
            except FileNotFoundError:current = None
            if operation == 'enable':
                module_ready(module)
                if not enabled():raise RuntimeError('Enable the private desktop runtime before main-login integration')
                forwarding_ready()
                lines = [line for line in common.splitlines() if not line.lstrip().startswith('#')]
                if sum('pam_systemd.so' in line for line in lines) != 1:
                    raise ValueError('The common session stack needs one native user-manager entry')
            value['phase'] = 'installing' if operation == 'enable' else 'removing'
            save(backup, value)  # Exact originals and targets precede every mutation.
            if operation == 'disable' and current is not None:
                os.unlink(policy.name, dir_fd=policy_directory);os.fsync(policy_directory)
            target = 'installed' if operation == 'enable' else 'original'
            for name in NAMES:
                contents = value['files'][name][target]
                if originals[name] != contents:atomic_write(pam_directory, name, contents.encode())
            if operation == 'enable':atomic_write(policy_directory, policy.name, expected.encode(), mode=0o600)
            value['phase'] = 'active' if operation == 'enable' else 'inactive'
            save(backup, value)
            return {'main_login': value['phase'], 'users': list(selected)}
        finally:
            os.close(pam_directory);os.close(policy_directory)


def main(module):
    parser = argparse.ArgumentParser(prog='karyo-main-login', allow_abbrev=False)
    parser.add_argument('operation', choices=('enable', 'disable'))
    parser.add_argument('users', nargs='*')
    args = parser.parse_args()
    try:
        result = activate(args.operation, args.users, module)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print('karyo-main-login: ' + str(error), file=sys.stderr)
        return 1
