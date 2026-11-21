# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
from dataclasses import replace
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.logind import Session, SessionError
from karyo_profiles.model import DesktopSettings, Ownership
from karyo_profiles.switching import DesktopStopped, ExistingSwitcher

HOME = LocalAccount("home", 1000, 1000, "/home/home", "/bin/bash")
WORK = LocalAccount("work", 1001, 1001, "/home/work", "/bin/bash")


class Store:
    def __init__(self, source="none", target="none"):
        self.policies = {"home": DesktopSettings(password=source), "work": DesktopSettings(password=target)}
        self.held = False

    @contextlib.contextmanager
    def locked(self):
        assert not self.held
        self.held = True
        try:
            yield self
        finally:
            self.held = False

    def ownership(self):
        assert self.held
        return Ownership.parse("home work\nother elsewhere\n")

    def settings(self, name):
        assert self.held
        return self.policies[name]


class Accounts:
    def __init__(self):
        self.current = HOME
        self.expiry = None
        self.hash = "$y$synthetic-fixture"

    def by_uid(self, uid):
        assert uid == 1000
        return self.current

    def account(self, name):
        return {"home": HOME, "work": WORK}[name]

    def expiry_day(self, name):
        return self.expiry

    def password_hash(self, name):
        return self.hash


class Sessions:
    def __init__(self, store):
        self.store = store
        self.rows = {"h1": Session("h1", "home", 1000, 7, True),
                     "w2": Session("w2", "work", 1001, 8, False)}
        self.calls = []
        self.confirm = True
        self.activate_changes = True
        self.before_get = None
        self.activation_error = None

    def all(self):
        assert self.store.held
        self.calls.append(("all",))
        return tuple(self.rows.values())

    def get(self, identifier, *, timeout):
        assert self.store.held and 0 < timeout <= 2
        self.calls.append(("get", identifier))
        if self.before_get:
            self.before_get(identifier)
        return self.rows.get(identifier)

    def lock(self, identifier, *, timeout):
        assert self.store.held and 0 < timeout <= 2
        self.calls.append(("lock", identifier))
        if self.confirm:
            self.rows[identifier] = replace(self.rows[identifier], locked=True)

    def activate(self, identifier, *, timeout):
        assert self.store.held and 0 < timeout <= 2
        self.calls.append(("activate", identifier))
        if self.activation_error:
            raise self.activation_error
        if self.activate_changes:
            self.rows = {key: replace(row, active=key == identifier) for key, row in self.rows.items()}


class SwitchTest(unittest.TestCase):
    def setup(self, source="none", target="none"):
        store, accounts = Store(source, target), Accounts()
        sessions = Sessions(store)
        now = [0.0]
        def sleep(interval):
            now[0] += max(interval, 0.05)
        controller = ExistingSwitcher(store, accounts, sessions, clock=lambda: now[0], sleep=sleep, today=lambda: 20000)
        self.store, self.accounts, self.sessions, self.controller = store, accounts, sessions, controller
        return controller

    def no_activation(self):
        self.assertFalse(any(call[0] == "activate" for call in self.sessions.calls))
        self.assertFalse(self.store.held)

    def test_unprotected_switch_has_no_password_lock_and_verifies_activation(self):
        controller = self.setup()
        self.assertEqual(controller.switch(HOME, "work"), {"user": "work", "vt": 8, "changed": True})
        self.assertFalse(any(call[0] == "lock" for call in self.sessions.calls))
        self.assertEqual(self.sessions.calls[-1], ("get", "w2"))

    def test_protected_target_is_confirmed_before_activation(self):
        for mode in ("home", "separate"):
            with self.subTest(mode=mode):
                controller = self.setup(target=mode)
                controller.switch(HOME, "work")
                calls = self.sessions.calls
                self.assertLess(calls.index(("lock", "w2")), calls.index(("activate", "w2")))
                self.assertIn(("get", "w2"), calls[:calls.index(("activate", "w2"))])

    def test_source_protection_is_locked_before_leaving(self):
        controller = self.setup(source="separate", target="home")
        controller.switch(HOME, "work")
        actions = [call for call in self.sessions.calls if call[0] in {"lock", "activate"}]
        self.assertEqual(actions, [("lock", "w2"), ("lock", "h1"), ("activate", "w2")])

    def test_current_desktop_is_noop_without_extra_authentication(self):
        controller = self.setup(source="home")
        self.assertFalse(controller.switch(HOME, "home")["changed"])
        self.assertEqual(self.sessions.calls, [("all",)])

    def test_cross_family_or_argument_injection_never_reaches_logind(self):
        for name in ("other", "elsewhere", "../work", "--help", "work\n"):
            with self.subTest(name=name):
                controller = self.setup()
                with self.assertRaises((ValueError, PermissionError)):
                    controller.switch(HOME, name)
                self.assertEqual(self.sessions.calls, [])

    def test_changed_caller_is_denied(self):
        controller = self.setup()
        self.accounts.current = replace(HOME, name="someone")
        with self.assertRaises(PermissionError):
            controller.switch(HOME, "work")
        self.assertEqual(self.sessions.calls, [])

    def test_expired_pending_or_locked_account_is_never_activated(self):
        for expiry, hashed in ((1, "$y$fixture"), (20000, "$y$fixture"), (None, "!"),
                               (None, "!$y$fixture"), (None, "*"), (None, "")):
            with self.subTest(expiry=expiry, locked=hashed[:1]):
                controller = self.setup()
                self.accounts.expiry, self.accounts.hash = expiry, hashed
                with self.assertRaises(PermissionError):
                    controller.switch(HOME, "work")
                self.assertEqual(self.sessions.calls, [])

    def test_missing_session_does_not_start_an_uncontrolled_replacement(self):
        controller = self.setup()
        del self.sessions.rows["w2"]
        with self.assertRaises(DesktopStopped):
            controller.switch(HOME, "work")
        self.no_activation()

    def test_ambiguous_target_is_not_selected_arbitrarily(self):
        controller = self.setup()
        self.sessions.rows["w3"] = Session("w3", "work", 1001, 9, False)
        with self.assertRaises(SessionError):
            controller.switch(HOME, "work")
        self.no_activation()

    def test_distinct_sessions_cannot_share_the_selected_vt(self):
        controller = self.setup()
        self.sessions.rows["w2"] = replace(self.sessions.rows["w2"], vt=7)
        with self.assertRaises(SessionError):
            controller.switch(HOME, "work")
        self.no_activation()

    def test_inactive_caller_does_not_switch(self):
        controller = self.setup()
        self.sessions.rows["h1"] = replace(self.sessions.rows["h1"], active=False)
        with self.assertRaises(DesktopStopped):
            controller.switch(HOME, "work")
        self.no_activation()

    def test_missing_lock_acknowledgement_hits_shared_deadline(self):
        controller = self.setup(target="separate")
        self.sessions.confirm = False
        with self.assertRaises(TimeoutError):
            controller.switch(HOME, "work")
        self.no_activation()

    def test_session_identity_changes_during_lock_are_denied(self):
        for changes in ({"uid": 2000}, {"name": "someone"}, {"vt": 12}, {"active": True}):
            with self.subTest(changes=changes):
                controller = self.setup(target="home")
                def change(identifier):
                    if identifier == "w2":
                        self.sessions.rows[identifier] = replace(self.sessions.rows[identifier], **changes)
                self.sessions.before_get = change
                with self.assertRaises(SessionError):
                    controller.switch(HOME, "work")
                self.no_activation()

    def test_lock_readiness_cannot_disappear_before_activation(self):
        controller = self.setup(target="home")
        reads = [0]
        def change(identifier):
            if identifier == "w2":
                reads[0] += 1
                if reads[0] == 2:
                    self.sessions.rows[identifier] = replace(self.sessions.rows[identifier], locked=False)
        self.sessions.before_get = change
        with self.assertRaises(SessionError):
            controller.switch(HOME, "work")
        self.no_activation()

    def test_successful_method_without_real_vt_change_is_not_success(self):
        controller = self.setup()
        self.sessions.activate_changes = False
        with self.assertRaises(TimeoutError):
            controller.switch(HOME, "work")
        self.assertEqual([call for call in self.sessions.calls if call[0] == "activate"], [("activate", "w2")])
        self.assertFalse(self.store.held)

    def test_native_activation_error_is_propagated(self):
        controller = self.setup(target="separate")
        self.sessions.activation_error = SessionError("Activation failed")
        with self.assertRaisesRegex(SessionError, "Activation failed"):
            controller.switch(HOME, "work")
        self.assertTrue(self.sessions.rows["w2"].locked)
        self.assertFalse(self.store.held)


if __name__ == "__main__":
    unittest.main()
