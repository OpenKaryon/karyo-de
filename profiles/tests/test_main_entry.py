# SPDX-License-Identifier: GPL-3.0-or-later
"""Main-entry lifecycle shapes, independent of native login-manager activation."""
from copy import deepcopy
from dataclasses import asdict, replace
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.journal_main import MainEntry
from karyo_profiles.journal_main_record import validate_main
from karyo_profiles.main_registry import MainRegistry
from karyo_profiles.user_manager import ManagerPending
from test_journal_session import ACCOUNT, LOGIN, active, prepared


def receipt():
    login = replace(LOGIN, service="lightdm")
    manager = active()["manager"]
    manager["live"]["login"] = asdict(login)
    return {"pending": None, "origin": {"pid": login.leader, "start": 501, "exe": [2, 51],
            "cgroup": "/system.slice/lightdm.service", "service": "lightdm"},
            "manager": manager, "login": asdict(login)}


class MainEntryRecordTest(unittest.TestCase):
    def test_prepare_attach_cancel_and_close_are_distinct_receipts(self):
        value = receipt()
        for candidate in (dict(value, pending="manager-prepare", manager=None, login=None),
                          dict(value, pending="entry-start", manager=prepared(), login=None),
                          value, dict(value, pending="login-close"), dict(value, pending="manager-close"),
                          dict(value, pending="login-close", manager=prepared(), login=None)):
            with self.subTest(pending=candidate["pending"]):validate_main(candidate, ACCOUNT)

    def test_parent_and_logind_receipts_follow_the_same_lifetime(self):
        value = receipt()
        changed = deepcopy(value); changed["origin"]["pid"] += 1
        with self.assertRaises(ValueError):validate_main(changed, ACCOUNT)
        changed = deepcopy(value); changed["manager"]["live"]["login"]["service"] = "karyo-desktop"
        with self.assertRaises(ValueError):validate_main(changed, ACCOUNT)
        with self.assertRaises(ValueError):validate_main(dict(value, pending="entry-start"), ACCOUNT)

    @patch("karyo_profiles.journal_main.process_gone", return_value=False)
    def test_close_hint_does_not_shorten_the_pam_parent_lifetime(self, gone):
        value = receipt()
        journal = Mock(); journal.record.return_value = {"main_login": value}
        dns = Mock(token="a"*32)
        manager, peer = Mock(), Mock()
        scope = MainEntry(journal, dns, manager, peer)
        scope.close_hint()
        self.assertEqual(journal.change.call_args.args[1]["main_login"]["pending"], "login-close")
        with self.assertRaises(ManagerPending):scope.close()
        manager.close.assert_not_called()
        dns.close.assert_not_called()

    @patch("karyo_profiles.journal_main.process_gone", return_value=True)
    def test_failed_pam_stack_waits_for_uncaptured_manager_without_stopping_it(self, gone):
        value = dict(receipt(), pending="login-close", manager=prepared(), login=None)
        journal = Mock(); journal.record.return_value = {"main_login": value}
        dns, manager = Mock(token="a"*32), Mock(identity=None)
        manager.inactive.return_value = False
        scope = MainEntry(journal, dns, manager)
        with self.assertRaises(ManagerPending):scope.close()
        manager.close.assert_not_called()
        journal.change.assert_not_called()


class MainRegistryTest(unittest.TestCase):
    @patch.object(MainEntry, 'restore')
    def test_draining_main_is_excluded_from_generic_recovery_until_natural_exit(self, restore):
        from contextlib import nullcontext
        journal, store = Mock(), Mock()
        store.locked.side_effect = lambda: nullcontext(store)
        record = {'token': 'a'*32, 'main_login': receipt()}
        journal.snapshot.return_value = {'profiles': [record, {'token': 'b'*32}]}
        journal.record.return_value = record
        scope = Mock(peer=None)
        restore.side_effect = [ManagerPending('Native user manager is draining'), scope]
        registry = MainRegistry(journal, store).restore()
        self.assertEqual(registry.tokens, {'a'*32})
        scope.close.side_effect = ManagerPending('Native process remains')
        registry.poll()
        self.assertEqual(registry.entries, {'a'*32: scope})
        self.assertFalse(registry.deferred)
        scope.dns.close.assert_not_called()
        registry.release_handles()
        scope.release_handles.assert_called_once()


if __name__ == "__main__":unittest.main()
