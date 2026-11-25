# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary networkd configuration, launch and native preflight lifetime."""
from contextlib import nullcontext
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock,patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.networkd_cooperation import MARKER,configured,launch,require_cooperation
from karyo_profiles.networks import NetworkError

CONFIG='[Network]\nManageForeignRoutes=no\nManageForeignRoutingPolicyRules=no\n'

class CooperationTest(unittest.TestCase):
    def command(self,text):
        command=Mock();command.bounded.side_effect=lambda seconds:nullcontext();command.run.return_value=text
        return command

    def test_later_administrator_setting_has_precedence(self):
        self.assertTrue(configured(CONFIG))
        self.assertFalse(configured(CONFIG+'ManageForeignRoutes=yes\n'))
        self.assertFalse(configured('[Network]\nManageForeignRoutes=no\n'))

    @patch('karyo_profiles.networkd_cooperation.program_identity',return_value=(1,2))
    def test_wrapper_passes_native_arguments_and_effective_configuration(self,_):
        args,env=launch(['--log-level=debug'],{'PATH':'/usr/bin',MARKER:'old'},configuration=CONFIG)
        self.assertEqual(args,['/usr/lib/systemd/systemd-networkd','--log-level=debug'])
        self.assertEqual(env[MARKER],'1')
        self.assertNotIn(MARKER,launch([],env,configuration=CONFIG+'ManageForeignRoutes=yes')[1])

    @patch('karyo_profiles.networkd_cooperation.program_identity',return_value=(1,2))
    def test_live_manager_uses_its_standard_service_uid_and_lifetime(self,_):
        command=self.command('ActiveState=active\nMainPID=123\nInvocationID='+'ab'*16+'\n')
        process=Mock();process.read.return_value=456;process.exited.return_value=False
        require_cooperation(command=command,process=process,service_uid=998,read=lambda pid:MARKER.encode()+b'=1\0')
        self.assertEqual(process.read.call_count,2)
        self.assertEqual(process.read.call_args.args[2],998)
        process.close.assert_called_once()

    @patch('karyo_profiles.networkd_cooperation.program_identity',return_value=(1,2))
    def test_existing_unwrapped_manager_requests_explicit_restart(self,_):
        command=self.command('ActiveState=active\nMainPID=123\nInvocationID='+'ab'*16+'\n');process=Mock()
        with self.assertRaisesRegex(NetworkError,'explicitly restart networkd'):
            require_cooperation(command=command,process=process,service_uid=998,read=lambda pid:b'PATH=/usr/bin\0')
        process.close.assert_called_once()

    def test_inactive_manager_needs_no_integration(self):
        process=Mock()
        require_cooperation(command=self.command('ActiveState=inactive\nMainPID=0\nInvocationID=\n'),process=process)
        process.open.assert_not_called()
