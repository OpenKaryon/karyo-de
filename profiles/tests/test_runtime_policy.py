# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles import runtime_policy as policy


class RuntimePolicyTest(unittest.TestCase):
    def test_missing_policy_is_disabled_but_malformed_policy_is_not_a_fallback(self):
        with patch.object(policy, "trusted_directory", side_effect=FileNotFoundError):
            self.assertFalse(policy.enabled())
        with patch.object(policy, "trusted_directory", return_value=123), \
                patch.object(policy.os, "close"), patch.object(policy, "root_file", return_value="enabled=true\n"), \
                patch.object(policy.os, "stat", return_value=Mock(st_nlink=1, st_mode=0o100600)):
            with self.assertRaises(ValueError):
                policy.enabled()

    def test_policy_rejects_public_or_hardlinked_files(self):
        for mode, links in ((0o100644, 1), (0o100600, 2)):
            with self.subTest(mode=mode, links=links), patch.object(policy, "trusted_directory", return_value=123), \
                    patch.object(policy.os, "close"), patch.object(policy, "root_file", return_value=policy.ENABLED), \
                    patch.object(policy.os, "stat", return_value=Mock(st_nlink=links, st_mode=mode)):
                with self.assertRaises(PermissionError):
                    policy.enabled()

    def test_disabled_apply_never_changes_forwarding(self):
        command = Mock()
        with patch.object(policy, "host_controller"), patch.object(policy, "enabled", return_value=False), \
                patch.object(policy, "dependencies") as dependencies:
            with self.assertRaises(PermissionError):
                policy.apply(command)
        dependencies.assert_not_called()
        command.run.assert_not_called()

    def test_failed_preflight_never_publishes_or_enables_anything(self):
        command, store = Mock(), Mock()
        store.locked.return_value = contextlib.nullcontext()
        with patch.object(policy, "host_controller"), \
                patch.object(policy, "dependencies", side_effect=RuntimeError("missing backend")), \
                patch.object(policy, "atomic_write") as write:
            with self.assertRaises(RuntimeError):
                policy.enable(command, store)
        write.assert_not_called()
        command.run.assert_not_called()

    def test_explicit_enable_is_idempotent_and_has_fixed_unit_order(self):
        command, store = Mock(), Mock()
        store.locked.return_value = contextlib.nullcontext()
        events = []
        command.run.side_effect = lambda *args: events.append(args)
        with patch.object(policy, "host_controller"), patch.object(policy, "dependencies"), \
                patch.object(policy, "enabled", return_value=True), \
                patch.object(policy, "forwarding_ready", side_effect=lambda: events.append(("verified",))), \
                patch.object(policy, "atomic_write") as write:
            self.assertEqual(policy.enable(command, store), {"isolated_desktops": "enabled"})
        write.assert_not_called()
        self.assertEqual(events, [
            ("/usr/bin/systemctl", "daemon-reload"),
            ("/usr/bin/systemctl", "enable", "--now", policy.NETWORK_UNIT),
            ("verified",),
            ("/usr/bin/systemctl", "enable", "--now", policy.SOCKET_UNIT),
        ])

    def test_apply_changes_only_the_explicit_global_forwarding_switch(self):
        command = Mock()
        with patch.object(policy, "host_controller"), patch.object(policy, "enabled", return_value=True), \
                patch.object(policy, "dependencies"), patch.object(policy, "forwarding_ready") as ready:
            policy.apply(command)
        command.run.assert_called_once_with("/usr/sbin/sysctl", "-w", "net.ipv4.ip_forward=1")
        ready.assert_called_once_with()

    def test_failed_network_unit_never_starts_controller_socket(self):
        command, store = Mock(), Mock()
        store.locked.return_value = contextlib.nullcontext()
        command.run.side_effect = ["", RuntimeError("unit failed")]
        with patch.object(policy, "host_controller"), patch.object(policy, "dependencies"), \
                patch.object(policy, "enabled", return_value=True), patch.object(policy, "forwarding_ready") as ready:
            with self.assertRaises(RuntimeError):
                policy.enable(command, store)
        ready.assert_not_called()
        self.assertEqual(command.run.call_count, 2)

    def test_no_inherited_namespace_can_enable_networking(self):
        command, store = Mock(), Mock()
        with patch.object(policy, "host_controller", side_effect=PermissionError("namespace")):
            with self.assertRaises(PermissionError):
                policy.enable(command, store)
            with self.assertRaises(PermissionError):
                policy.apply(command)
        store.locked.assert_not_called()
        command.run.assert_not_called()
