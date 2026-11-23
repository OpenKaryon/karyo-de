# SPDX-License-Identifier: GPL-3.0-or-later
"""A disappearing unrelated login must not corrupt desktop or console inventory."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.logind import SessionError, Sessions


class ChurnCommand:
    def __init__(self, failure=SessionError):
        self.lists = 0
        self.calls = []
        self.failure = failure

    def run(self, *args, timeout):
        self.calls.append((args, timeout))
        if timeout <= 0:
            raise TimeoutError("Fixture inventory deadline expired")
        if args[0] == "list-sessions":
            self.lists += 1
            # The first inventory has a disappearing SSH login after Home.
            ids = ("home", "ssh") if self.lists == 1 else ("home", "work")
            return json.dumps([{"session": identifier} for identifier in ids])
        identifier = args[1]
        if identifier == "ssh":
            raise self.failure("Listed SSH session ended")
        # Home moves consoles between inventories. Reusing any partial result
        # would falsely reserve the old console or select the wrong switch VT.
        vt = "7" if self.lists == 1 else ("9" if identifier == "home" else "8")
        values = dict(Id=identifier, Name=identifier, User="1001", Type="wayland", Seat="seat0",
                      VTNr=vt, Active="yes", State="active", Class="user", Remote="no", LockedHint="no",
                      Leader="1234", Scope="session-" + identifier + ".scope", Service="login")
        requested = [option.removeprefix("--property=") for option in args[3:]]
        return "".join(key + "=" + values[key] + "\n" for key in requested)


class InventoryChurnTest(unittest.TestCase):
    def test_graphical_retry_discards_partial_results_and_rereads_home(self):
        command = ChurnCommand()
        result = Sessions(command).all()
        self.assertEqual([(s.id, s.vt) for s in result], [("home", 9), ("work", 8)])
        self.assertEqual(command.lists, 2)

    def test_console_retry_does_not_reserve_a_stale_partial_console(self):
        command = ChurnCommand()
        self.assertEqual(Sessions(command).occupied_vts(), frozenset({8, 9}))
        self.assertEqual(command.lists, 2)

    def test_greeter_retry_replaces_the_whole_partial_inventory(self):
        class Greeters(ChurnCommand):
            def run(self, *args, timeout):
                text = super().run(*args, timeout=timeout)
                return text.replace("User=1001\n", "User=108\n").replace("Class=user\n", "Class=greeter\n")
        command = Greeters()
        self.assertEqual([(s.id, s.vt) for s in Sessions(command).greeters()], [("home", 9), ("work", 8)])
        self.assertEqual(command.lists, 2)

    def test_owner_retry_discards_partial_results(self):
        command = ChurnCommand()
        self.assertEqual([s.id for s in Sessions(command).owners(1001)], ["home", "work"])
        self.assertEqual(command.lists, 2)

    def test_persistent_unreadable_login_fails_closed_after_three_inventories(self):
        class Unreadable(ChurnCommand):
            def run(self, *args, timeout):
                if args[0] == "show-session":
                    raise SessionError("Service unavailable")
                return super().run(*args, timeout=timeout)
        for method in ("all", "greeters", "occupied_vts", "owners"):
            command = Unreadable()
            with self.subTest(method=method), self.assertRaises(SessionError):
                getattr(Sessions(command), method)(*([1001] if method == "owners" else []))
            self.assertEqual(command.lists, 3)

    def test_unavailable_list_does_not_establish_absence(self):
        class Unavailable(ChurnCommand):
            def run(self, *args, timeout):
                self.lists += 1
                raise SessionError("Service unavailable")
        command = Unavailable()
        with self.assertRaises(SessionError):
            Sessions(command).all()
        self.assertEqual(command.lists, 3)

    def test_malformed_metadata_and_timeout_are_not_retried(self):
        for failure in (ValueError, TimeoutError):
            command = ChurnCommand(failure)
            with self.subTest(failure=failure), self.assertRaises(failure):
                Sessions(command).all()
            self.assertEqual(command.lists, 1)

    def test_retries_share_the_original_deadline_including_list_requests(self):
        command = ChurnCommand()
        # Initial list, Home, failed SSH, retry check, fresh list: no new 5s budget.
        clock = iter((10, 10, 11, 12, 14, 16)).__next__
        with self.assertRaises(TimeoutError):
            Sessions(command, clock=clock).all()
        self.assertEqual(command.calls[-1][0][0], "list-sessions")
        self.assertEqual(command.calls[-1][1], -1)


if __name__ == "__main__":
    unittest.main()
