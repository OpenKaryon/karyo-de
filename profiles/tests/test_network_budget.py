# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary multi-command deadlines, without touching the host network."""
from pathlib import Path
import subprocess
import sys
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.networks import NetworkCommand


class NetworkBudgetTests(unittest.TestCase):
    def test_several_commands_share_the_same_deadline(self):
        command = NetworkCommand()
        with command.bounded(2):
            first = command._deadline
            self.assertEqual(command.run('/usr/bin/printf', '%s', 'first'), 'first')
            self.assertEqual(command.run('/usr/bin/printf', '%s', 'second'), 'second')
            self.assertEqual(command._deadline, first)
        self.assertIsNone(command._deadline)

    def test_nested_budget_cannot_extend_the_outer_deadline(self):
        command = NetworkCommand()
        with command.bounded(2):
            first = command._deadline
            with command.bounded(5):
                self.assertEqual(command._deadline, first)
            with command.bounded(.1):
                self.assertLess(command._deadline, first)
            self.assertEqual(command._deadline, first)

    def test_expired_batch_does_not_launch_another_command(self):
        command = NetworkCommand()
        with command.bounded(.02):
            time.sleep(.03)
            with patch('karyo_profiles.networks.subprocess.Popen') as launch:
                with self.assertRaises(TimeoutError):
                    command.run('/usr/bin/true')
                launch.assert_not_called()
        self.assertEqual(command.run('/usr/bin/printf', 'after'), 'after')

    def test_slow_native_child_is_reaped_and_budget_restored(self):
        command = NetworkCommand()
        real_launch = subprocess.Popen
        children = []
        def capture(*args, **kwargs):
            child = real_launch(*args, **kwargs)
            children.append(child)
            return child
        with patch('karyo_profiles.networks.subprocess.Popen', side_effect=capture):
            with self.assertRaises(TimeoutError):
                with command.bounded(.1):
                    command.run('/usr/bin/sleep', '2')
        self.assertEqual(len(children), 1)
        self.assertIsNotNone(children[0].poll())
        self.assertIsNone(command._deadline)


if __name__ == '__main__':
    unittest.main()
