# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.journal import JournalRetained
from karyo_profiles.journal_netbird import restore_offline_services, restore_optional
from karyo_profiles.user_manager import UserManagerScope
import test_graphical as graphical_fixture
import test_journal_session as session_fixture
import test_netbird as provider_fixture
import test_returning as returning_fixture


class ProviderConsumersTest(unittest.TestCase):
    def scope(self):
        scope = session_fixture.ConsumersTest().scope(group="/foreign/session.scope")
        scope.provider = Mock()
        scope.provider.scope.unit.identity.pid = os.getpid()
        return scope

    def test_exact_verified_provider_is_accepted_outside_graphical_cgroups(self):
        scope = self.scope(); scope.verify_consumers()
        self.assertEqual(scope.provider.verify.call_count, 2)
        scope.provider.close.assert_not_called()

    def test_other_pid_is_not_accepted_by_same_provider_or_uid(self):
        scope = self.scope(); scope.provider.scope.unit.identity.pid = 2147483646
        with self.assertRaises(JournalRetained): scope.verify_consumers()
        scope.provider.close.assert_not_called()

    def test_provider_is_reverified_before_and_after_inventory(self):
        for calls in ([JournalRetained("Changed provider")], [None, JournalRetained("Changed after inventory")]):
            scope = self.scope(); scope.provider.verify.side_effect = calls
            with self.assertRaises(JournalRetained): scope.verify_consumers()
            scope.provider.close.assert_not_called()


class ProviderPolicyTest(unittest.TestCase):
    def manager(self, enabled):
        files = provider_fixture.files()
        files.plan.client_properties.return_value = ("NetworkNamespacePath=" + files.network.path,)
        return UserManagerScope(files, netbird=enabled)

    def test_endpoint_is_derived_for_both_manager_and_graphical_entry(self):
        manager = self.manager(True)
        expected = "KARYO_NETBIRD_ENDPOINT=/run/karyo-netbird-" + "ab" * 16 + "/client.json"
        self.assertEqual(manager.environment, (expected,))
        self.assertIn(("Environment=" + expected + "\n").encode(), manager.contents)
        fixture = graphical_fixture.GraphicalTest()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        fixture.lease.manager.environment = manager.environment
        fixture.start()
        launch = next(call for call in fixture.kernel.calls if call[0].endswith("systemd-run"))
        self.assertIn("--setenv=" + expected, launch)

    def test_plain_profiles_do_not_gain_provider_environment_or_ambiguous_policy(self):
        manager = self.manager(False)
        self.assertEqual(manager.environment, ())
        self.assertNotIn(b"KARYO_NETBIRD_ENDPOINT", manager.contents)
        with self.assertRaises(ValueError): self.manager("yes")


class ProviderRecoveryTest(unittest.TestCase):
    def fixture(self):
        journal, network, dns, provider = Mock(), Mock(), Mock(), Mock()
        journal.record.return_value = {"provider": {"kind": "netbird"}}
        dns.services.resolver.identity.pid = 100
        provider.scope.unit.identity.pid = 101
        network.network._ip.return_value = "100\n101\n"
        return journal, network, dns, provider

    def test_optional_restore_does_not_adopt_unrecorded_provider(self):
        journal = Mock(); journal.record.return_value = {}
        with patch("karyo_profiles.journal_netbird.JournalNetBird.restore") as restore:
            self.assertIsNone(restore_optional(journal, Mock()))
            restore.assert_not_called()

    def test_offline_restore_accepts_only_both_verified_services(self):
        journal, network, dns, provider = self.fixture()
        with patch("karyo_profiles.journal_netbird.JournalDNS.restore_observers", return_value=dns), \
                patch("karyo_profiles.journal_netbird.JournalNetBird.restore", return_value=provider):
            self.assertEqual(restore_offline_services(journal, network), (dns, provider))
        self.assertEqual(provider.verify.call_count, 2)
        dns.close.assert_not_called(); provider.close.assert_not_called()

    def test_unknown_offline_consumer_retains_native_objects_and_releases_observers(self):
        journal, network, dns, provider = self.fixture()
        network.network._ip.return_value = "100\n101\n102\n"
        with patch("karyo_profiles.journal_netbird.JournalDNS.restore_observers", return_value=dns), \
                patch("karyo_profiles.journal_netbird.JournalNetBird.restore", return_value=provider):
            with self.assertRaises(JournalRetained): restore_offline_services(journal, network)
        dns.release_handles.assert_called_once(); provider.release_handles.assert_called_once()
        dns.close.assert_not_called(); provider.close.assert_not_called()

    def test_logout_closes_session_then_provider_then_dns_and_network(self):
        registry, scope = returning_fixture.PollTest().fixture()
        scope.graph.process.exited.return_value = True
        scope.provider = Mock(); scope.forwarding = Mock()
        events = []
        for name, operation in (("session", scope.close), ("provider", scope.provider.close),
                                ("dns", scope.dns.close), ("forwarding", scope.forwarding.close),
                                ("network", scope.network.close)):
            operation.side_effect = lambda name=name: events.append(name)
        with patch("karyo_profiles.returning.ReturnAfterExit"): registry.poll()
        self.assertEqual(events, ["session", "provider", "dns", "forwarding", "network"])
        self.assertEqual(registry.profiles, {})


    def test_changed_provider_blocks_session_before_manager_intent_or_preparation(self):
        from karyo_profiles.journal_session import JournalSession
        journal, dns, manager = Mock(), Mock(), Mock()
        journal.record.return_value = {"dns": {"pending": None}, "provider": {"kind": "netbird"}}
        dns.network.network.fd = None
        manager.fd = manager.pidfd = None
        with patch("karyo_profiles.journal_session.UserManagerScope", return_value=manager), \
                patch("karyo_profiles.journal_session.restore_optional", side_effect=JournalRetained("Changed provider")):
            with self.assertRaises(JournalRetained): JournalSession.start(journal, dns)
        manager.prepare.assert_not_called(); journal.change.assert_not_called()
        dns.release_handles.assert_called_once()

    def test_unknown_provider_close_failure_keeps_dns_and_network(self):
        registry, scope = returning_fixture.PollTest().fixture()
        scope.graph.process.exited.return_value = True
        scope.provider = Mock(); scope.provider.close.side_effect = JournalRetained("Changed provider")
        with patch("karyo_profiles.returning.ReturnAfterExit"):
            with self.assertRaises(JournalRetained): registry.poll()
        scope.dns.close.assert_not_called(); scope.network.close.assert_not_called()
        self.assertIn("token", registry.profiles)
