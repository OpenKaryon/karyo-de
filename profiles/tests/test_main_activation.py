# SPDX-License-Identifier: GPL-3.0-or-later
"""Normal enable, interrupted publication and exact configuration restoration."""
from contextlib import nullcontext
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles import main_activation as activation
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.model import Ownership

ORIGINAL = 'auth required pam_unix.so\n@include common-account\nsession required pam_limits.so\n@include common-session\n@include common-password\n'


@unittest.skipUnless(os.geteuid() == 0, 'native root temporary configuration fixture')
class ActivationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir='/root')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.pam = self.root/'pam';self.pam.mkdir()
        for name in activation.NAMES:(self.pam/name).write_text(ORIGINAL)
        (self.pam/'common-session').write_text('session optional pam_systemd.so\n')
        self.policy = self.root/'policy/main_login.conf'
        self.module = self.root/'security/pam_karyo_network.so';self.module.parent.mkdir()
        self.module.write_bytes(b'\x7fELFfixture')
        self.command = Mock();self.command.run.return_value = 'inactive\n'
        self.account = LocalAccount('home', 16098, 16098, '/home/home', '/bin/bash')
        self.accounts = Mock();self.accounts.account.return_value = self.account
        self.store = SimpleNamespace(locked=nullcontext, ownership=lambda: Ownership.parse('home\n'))
        for name, options in (('offline', {}), ('enabled', {'return_value': True}), ('forwarding_ready', {})):
            patched = patch.object(activation, name, **options)
            patched.start();self.addCleanup(patched.stop)

    def run_operation(self, operation):
        return activation.activate(operation, ['home'] if operation == 'enable' else [], self.module,
            store=self.store, accounts=self.accounts, sessions=Mock(), command=self.command,
            pam=self.pam, state=self.root/'state', policy=self.policy)

    def test_enable_and_disable_preserve_auth_account_password_and_common_session(self):
        self.assertEqual(self.run_operation('enable')['main_login'], 'active')
        for name in activation.NAMES:
            result = (self.pam/name).read_text()
            self.assertEqual(result, activation.patched(ORIGINAL, self.module))
            self.assertIn('prepare desktop=karyo\n@include common-session\nsession requisite', result)
        self.assertEqual(json.loads(self.policy.read_text()), {'version': 1, 'users': ['home']})
        self.assertEqual(self.policy.stat().st_mode & 0o777, 0o600)
        self.run_operation('disable')
        self.assertFalse(self.policy.exists())
        for name in activation.NAMES:self.assertEqual((self.pam/name).read_text(), ORIGINAL)
        self.assertEqual((self.pam/'common-session').read_text(), 'session optional pam_systemd.so\n')

    def test_interrupted_install_replays_only_recorded_targets(self):
        original_write = activation.atomic_write
        def interrupted(directory, name, data, **options):
            original_write(directory, name, data, **options)
            if name == 'lightdm':raise RuntimeError('Interrupted after the first PAM replacement')
        with patch.object(activation, 'atomic_write', side_effect=interrupted):
            with self.assertRaises(RuntimeError):self.run_operation('enable')
        self.assertFalse(self.policy.exists())
        self.assertEqual((self.pam/'lightdm-autologin').read_text(), ORIGINAL)
        self.assertEqual(self.run_operation('enable')['main_login'], 'active')
        self.run_operation('disable')
        self.assertEqual((self.pam/'lightdm').read_text(), ORIGINAL)

    def test_later_common_session_update_does_not_prevent_own_integration_removal(self):
        self.run_operation('enable')
        (self.pam/'common-session').write_text('session optional pam_systemd.so\nsession optional pam_umask.so\n')
        self.run_operation('disable')
        self.assertIn('pam_umask.so', (self.pam/'common-session').read_text())
        self.assertEqual((self.pam/'lightdm').read_text(), ORIGINAL)

    def test_running_display_manager_requires_explicit_maintenance_before_writes(self):
        self.command.run.return_value = 'active\n'
        with self.assertRaisesRegex(RuntimeError, 'Stop LightDM'):self.run_operation('enable')
        self.assertFalse((self.root/'state').exists())
        self.assertEqual((self.pam/'lightdm').read_text(), ORIGINAL)

    def test_disabled_integration_can_select_a_different_main_account(self):
        self.run_operation('enable')
        self.run_operation('disable')
        other = LocalAccount('other', 16097, 16097, '/home/other', '/bin/bash')
        self.accounts.account.return_value = other
        result = activation.activate('enable', ['other'], self.module, store=self.store,
            accounts=self.accounts, sessions=Mock(), command=self.command, pam=self.pam,
            state=self.root/'state', policy=self.policy)
        self.assertEqual(result['users'], ['other'])
        self.assertEqual(json.loads(self.policy.read_text())['users'], ['other'])
        self.run_operation('disable')
        self.assertEqual((self.pam/'lightdm').read_text(), ORIGINAL)


if __name__ == '__main__':unittest.main()
