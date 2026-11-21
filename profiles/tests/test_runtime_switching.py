# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles import runtime_switching as runtime
from karyo_profiles.journal import JournalRetained
from karyo_profiles.logind import SessionError
from karyo_profiles.switching import DesktopStopped
from test_switching import HOME, WORK, Accounts, Sessions, Store


class RuntimeSwitchTest(unittest.TestCase):
    def setup(self):
        self.store = Store()
        self.accounts = Accounts()
        self.sessions = Sessions(self.store)
        del self.sessions.rows["w2"]
        self.registry = Mock(store=self.store, profiles={})
        self.registry.journal.snapshot.return_value = {"profiles": []}
        self.switcher = runtime.RuntimeSwitcher(self.store, registry=self.registry,
                                               accounts=self.accounts, sessions=self.sessions)
        self.events = []
        self.network = Mock(token="owned")
        self.network.network.fd = 23
        self.dns = Mock()
        self.scope = Mock(token="owned")
        self.scope.enter.return_value = {"user": "work", "vt": 8, "starting": True, "changed": True}
        self.patches = contextlib.ExitStack()
        self.addCleanup(self.patches.close)
        for name, result in (("enabled", True), ("forwarding_ready", None),
                             ("supported_resolver_layout", "stub-resolv.conf"),
                             ("discover_underlay", "physical"), ("discover_dns", "dns-seed")):
            self.patches.enter_context(patch.object(runtime, name, return_value=result))
        for name, result in (("JournalNetwork", self.network), ("JournalForwarding", Mock()),
                             ("JournalDNS", self.dns), ("JournalSession", self.scope)):
            def start(*args, _name=name, _result=result, **kwargs):
                self.assertTrue(self.store.held)
                self.events.append(_name)
                return _result
            self.patches.enter_context(patch.object(runtime, name)).start.side_effect = start

    def test_public_switch_allocates_in_order_and_registers_before_enter(self):
        self.setup()
        def enter(caller, **options):
            self.assertTrue(self.store.held)
            self.assertIs(self.registry.profiles["owned"], self.scope)
            self.assertEqual(caller, HOME)
            self.assertIs(options["reauthorize"], authorize)
            return {"user": "work", "starting": True}
        authorize = Mock(return_value=HOME)
        self.scope.enter.side_effect = enter
        self.assertEqual(self.switcher.switch(HOME, "work", reauthorize=authorize),
                         {"user": "work", "starting": True})
        self.assertEqual(self.events, ["JournalNetwork", "JournalForwarding", "JournalDNS", "JournalSession"])
        self.assertFalse(self.store.held)

    def test_running_session_does_not_allocate_or_require_networking_policy(self):
        self.setup()
        from karyo_profiles.logind import Session
        self.sessions.rows["w2"] = Session("w2", "work", 1001, 8, False)
        self.assertTrue(self.switcher.switch(HOME, "work")["changed"])
        self.assertEqual(self.events, [])
        runtime.enabled.assert_not_called()

    def test_disabled_or_unready_network_never_allocates(self):
        for phase in ("enabled", "forwarding_ready", "discover_dns"):
            with self.subTest(phase=phase):
                self.setup()
                if phase == "enabled":
                    runtime.enabled.return_value = False
                    error = DesktopStopped
                else:
                    getattr(runtime, phase).side_effect = RuntimeError("unavailable")
                    error = RuntimeError
                with self.assertRaises(error):
                    self.switcher.start(HOME, "work")
                self.assertEqual(self.events, [])
                self.patches.close()

    def test_foreign_main_locked_expired_or_missing_source_never_starts(self):
        for case in ("foreign", "main", "locked", "expired", "source"):
            with self.subTest(case=case):
                self.setup()
                target = "work"
                if case == "foreign": target = "elsewhere"
                if case == "main": target = "home"
                if case == "locked": self.accounts.hash = "!fixture"
                if case == "expired": self.accounts.expiry = 1
                if case == "source": self.sessions.rows.clear()
                with self.assertRaises((PermissionError, DesktopStopped)):
                    self.switcher.start(HOME, target)
                self.assertEqual(self.events, [])
                self.patches.close()

    def test_configured_vpn_is_not_silently_started_without_its_provider(self):
        self.setup()
        from karyo_profiles.model import DesktopSettings
        self.store.policies["work"] = DesktopSettings(vpn="netbird")
        with self.assertRaisesRegex(RuntimeError, "provider"):
            self.switcher.start(HOME, "work")
        self.assertEqual(self.events, [])

    def test_lost_authorization_before_allocation_never_starts(self):
        self.setup()
        authorize = Mock(side_effect=[HOME, WORK])
        with self.assertRaises(PermissionError):
            self.switcher.start(HOME, "work", reauthorize=authorize)
        self.assertEqual(self.events, [])

    def test_retained_profile_receipt_blocks_duplicate_start(self):
        self.setup()
        self.registry.journal.snapshot.return_value = {"profiles": [{"account": {"uid": 1001}}]}
        with self.assertRaises(JournalRetained):
            self.switcher.start(HOME, "work")
        self.assertEqual(self.events, [])

    def test_enter_failure_keeps_fully_owned_session_registered(self):
        self.setup()
        self.scope.enter.side_effect = PermissionError("caller ended")
        with self.assertRaises(PermissionError):
            self.switcher.start(HOME, "work")
        self.assertIs(self.registry.profiles["owned"], self.scope)
        self.scope.release_handles.assert_not_called()
        self.dns.release_handles.assert_not_called()

    def test_partial_start_closes_observers_without_erasing_receipts(self):
        self.setup()
        runtime.JournalSession.start.side_effect = RuntimeError("session failed")
        with patch.object(runtime.os, "close") as close, self.assertRaises(RuntimeError):
            self.switcher.start(HOME, "work")
        self.dns.release_handles.assert_called_once_with()
        close.assert_called_once_with(23)
        self.assertIsNone(self.network.network.fd)
        self.registry.journal.change.assert_not_called()
        self.network.close.assert_not_called()
        self.assertEqual(self.registry.profiles, {})

    def test_cannot_mix_registry_and_configuration_locks(self):
        with self.assertRaises(ValueError):
            runtime.RuntimeSwitcher(object(), registry=Mock(store=object()))
