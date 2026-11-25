# SPDX-License-Identifier: GPL-3.0-or-later
"""LightDM's running allocation range stays outside the child console pool."""
from contextlib import nullcontext
from pathlib import Path
import sys
import subprocess
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.display_manager import MARKER, configured_minimum, launch, require_partition
from karyo_profiles.logind import SessionError


class PartitionTest(unittest.TestCase):
    def command(self, text):
        result = Mock();result.bounded.side_effect=lambda seconds:nullcontext()
        result.run.return_value=text
        return result

    @patch('karyo_profiles.display_manager.program_identity', return_value=(1, 2))
    @patch('karyo_profiles.display_manager.os.getuid', return_value=0)
    @patch('karyo_profiles.display_manager.os.geteuid', return_value=0)
    def test_native_configuration_diagnostic_stream_is_parsed(self, *_):
        native = subprocess.run
        def diagnostic(arguments, **options):
            return native([sys.executable, '-c', "import sys;sys.stderr.write('[LightDM]\\nA minimum-vt=13\\n')"], **options)
        with patch('karyo_profiles.display_manager.subprocess.run', side_effect=diagnostic):
            _,environment=launch([], {})
        self.assertEqual(environment[MARKER], '13')

    @patch('karyo_profiles.display_manager.program_identity', return_value=(1, 2))
    @patch('karyo_profiles.display_manager.os.getuid', return_value=0)
    @patch('karyo_profiles.display_manager.os.geteuid', return_value=0)
    def test_wrapper_preserves_arguments_and_publishes_only_its_effective_partition(self, *_):
        command=self.command('   [Seat:*]\nA  minimum-vt=7\n   [LightDM]\nB  minimum-vt=13\n')
        args,env=launch(['--debug'], {'PATH':'/usr/bin',MARKER:'7'}, command=command)
        self.assertEqual(args, ['/usr/sbin/lightdm','--debug'])
        self.assertEqual(env[MARKER], '13')
        self.assertEqual(env['PATH'], '/usr/bin')
        self.assertEqual(configured_minimum('  [Seat:*]\nA minimum-vt=13'), None)

    @patch('karyo_profiles.display_manager.program_identity', return_value=(1, 2))
    def test_running_wrapped_manager_is_checked_in_its_own_lifetime(self, _):
        command=self.command('ActiveState=active\nMainPID=123\nInvocationID='+'ab'*16+'\n')
        process=Mock();process.read.return_value=456;process.exited.return_value=False
        require_partition(command=command,process=process,read=lambda pid:MARKER.encode()+b'=13\0')
        self.assertEqual(process.read.call_count, 2)
        process.close.assert_called_once_with(process.open.return_value)

    @patch('karyo_profiles.display_manager.program_identity', return_value=(1, 2))
    def test_legacy_running_manager_requires_explicit_integration_before_new_child(self, _):
        command=self.command('ActiveState=active\nMainPID=123\nInvocationID='+'ab'*16+'\n')
        process=Mock()
        with self.assertRaisesRegex(SessionError,'restart the display manager'):
            require_partition(command=command,process=process,read=lambda pid:b'PATH=/usr/bin\0')
        process.close.assert_called_once()

    def test_no_running_lightdm_needs_no_process_observer(self):
        command=self.command('ActiveState=inactive\nMainPID=0\nInvocationID=\n');process=Mock()
        require_partition(command=command,process=process)
        process.open.assert_not_called()
