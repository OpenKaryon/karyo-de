# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.logind import LoginCommand, Session, SessionError, Sessions, authorized_caller, listing, process_session
from karyo_profiles.model import DesktopSettings, Ownership


def properties(identifier="c2", name="home", uid="1000", **changes):
    values = dict(Id=identifier, Name=name, User=uid, Type="wayland", Seat="seat0", VTNr="7",
                  Active="yes", State="active", Class="user", Remote="no", LockedHint="no")
    values.update(changes)
    return "".join(key + "=" + value + "\n" for key, value in values.items())


HOME = LocalAccount("home", 1000, 1000, "/home/home", "/bin/bash")
WORK = LocalAccount("work", 1001, 1001, "/home/work", "/bin/bash")


class Accounts:
    def by_uid(self, uid):
        if uid != 1000:
            raise ValueError("Unexpected fixture caller")
        return HOME

    def account(self, name, *, missing_ok=False):
        return {"home": HOME, "work": WORK}.get(name)

    def caption(self, name):
        return {"home": "Main account", "work": "Work"}[name]


class Command:
    def __init__(self, rows=None):
        self.rows = rows if rows is not None else {"c2": properties()}
        self.calls = []

    def run(self, *arguments, timeout):
        self.calls.append((arguments, timeout))
        if timeout <= 0:
            raise TimeoutError("Fixture deadline")
        if arguments[0] == "list-sessions":
            return json.dumps([{"session": identifier} for identifier in self.rows])
        expected = {"--property=" + name for name in ("Id", "Name", "User", "Type", "Seat", "VTNr", "Active", "State", "Class", "Remote", "LockedHint")}
        if set(arguments[3:]) != expected:
            raise AssertionError("Each loginctl property needs its own option")
        return self.rows[arguments[1]]


class Store:
    def ownership(self):
        return Ownership.parse("home work missing\nother otherwork\n")

    def settings(self, name):
        return DesktopSettings(workstation=name == "work")


class LogindTest(unittest.TestCase):
    def test_graphical_inventory_filters_remote_greeter_and_closing_sessions(self):
        command = Command({"c2": properties(), "c3": properties("c3", "work", "1001", Active="no", State="online", VTNr="8"),
                           "s4": properties("s4", Type="tty"), "g5": properties("g5", Class="greeter"),
                           "r6": properties("r6", Remote="yes"), "c7": properties("c7", State="closing")})
        found = Sessions(command).all()
        self.assertEqual([value.name for value in found], ["home", "work"])
        self.assertEqual(found[1].vt, 8)
        self.assertFalse(found[1].active)
        self.assertTrue(all(0 < timeout <= 5 for _, timeout in command.calls))

    def test_literal_properties_require_matching_id_and_unique_known_keys(self):
        for text in (properties(identifier="different"), properties() + "Name=work\n",
                     properties() + "Unknown=yes\n", "not properties"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                Session.parse(text, "c2")

    def test_invalid_identity_and_vt_are_not_running_desktops(self):
        for values in (dict(uid="0"), dict(uid="65534"), dict(uid="1000x"), dict(VTNr="0"),
                       dict(VTNr="64"), dict(VTNr="-1"), dict(Active="maybe"), dict(LockedHint="unknown"), dict(name="--help")):
            with self.subTest(values=values), self.assertRaises(ValueError):
                Session.parse(properties(**values), "c2")

    def test_no_shell_options_paths_duplicate_or_unbounded_session_list(self):
        invalid = [None, {}, [None], [{"session": "--help"}], [{"session": "../c2"}],
                   [{"session": "c2"}, {"session": "c2"}], [{"session": "c2"}] * 257]
        for rows in invalid:
            with self.subTest(rows=str(rows)[:80]), patch.object(Command, "run", return_value=json.dumps(rows)):
                with self.assertRaises(ValueError):
                    Sessions(Command()).all()

    def test_lock_hint_is_literal_and_required_for_graphical_sessions(self):
        self.assertTrue(Session.parse(properties(LockedHint="yes"), "c2").locked)
        self.assertFalse(Session.parse(properties(LockedHint="no"), "c2").locked)
        with self.assertRaises(ValueError):
            Session.parse(properties().replace("LockedHint=no\n", ""), "c2")

    def test_control_actions_accept_only_validated_session_identifiers(self):
        command = Command()
        with patch.object(command, "run", return_value="") as run:
            sessions = Sessions(command)
            sessions.lock("c2", timeout=1)
            sessions.activate("c3", timeout=1)
            self.assertEqual([call.args for call in run.call_args_list], [("lock-session", "c2"), ("activate", "c3")])
            for identifier in ("--help", "../c2", "c2\n"):
                with self.assertRaises(ValueError):
                    sessions.activate(identifier)
            self.assertEqual(run.call_count, 2)

    def test_shared_total_deadline_instead_of_timeout_per_session(self):
        command = Command({"c2": properties(), "c3": properties("c3")})
        with self.assertRaises(TimeoutError):
            Sessions(command, clock=iter((10, 14, 16)).__next__).all()
        self.assertEqual(command.calls[1][1], 1)

    def test_ending_session_is_error_instead_of_false_stopped_state(self):
        command = Command()
        original = command.run
        def run(*args, timeout):
            if args[0] == "show-session":
                raise SessionError("Session disappeared")
            return original(*args, timeout=timeout)
        command.run = run
        with self.assertRaises(SessionError):
            Sessions(command).all()

    def test_family_listing_omits_missing_account_and_excludes_other_family(self):
        command = Command({"c2": properties(), "c3": properties("c3", "work", "1001", Active="no", VTNr="8")})
        items = listing(HOME, Store(), Accounts(), Sessions(command))
        self.assertEqual([item["user"] for item in items], ["home", "work"])
        self.assertEqual([item["label"] for item in items], ["Home", "Work"])
        self.assertEqual(items[1], dict(user="work", label="Work", running=True, active=False, vt=8, current=False))

    def test_stale_uid_session_cannot_impersonate_recreated_account(self):
        command = Command({"c2": properties("c2", "work", "1002")})
        items = listing(HOME, Store(), Accounts(), Sessions(command))
        self.assertFalse(items[1]["running"])

    def test_ambiguous_inactive_sessions_fail_but_one_active_wins(self):
        command = Command({"c2": properties(Active="no"), "c3": properties("c3", Active="no", VTNr="8")})
        with self.assertRaises(ValueError):
            listing(HOME, Store(), Accounts(), Sessions(command))
        command.rows["c3"] = properties("c3", VTNr="8")
        self.assertEqual(listing(HOME, Store(), Accounts(), Sessions(command))[0]["vt"], 8)

    def test_caller_requires_root_and_canonical_ordinary_pkexec_uid(self):
        for uid, euid, environment in ((1000, 0, {"PKEXEC_UID": "1000"}), (0, 1000, {"PKEXEC_UID": "1000"}),
                                      (0, 0, {}), (0, 0, {"PKEXEC_UID": "01000"}),
                                      (0, 0, {"PKEXEC_UID": "+1000"})):
            with self.subTest(environment=environment), patch("os.getuid", return_value=uid), patch("os.geteuid", return_value=euid):
                with self.assertRaises(PermissionError):
                    authorized_caller(environment, accounts=Accounts(), sessions=Sessions(Command()), identify=lambda: "c2")

    def test_active_session_kernel_identity_overrides_forged_environment(self):
        with patch("os.getuid", return_value=0), patch("os.geteuid", return_value=0):
            environment = {"PKEXEC_UID": "1000", "XDG_SESSION_ID": "fake", "USER": "root"}
            self.assertEqual(authorized_caller(environment, accounts=Accounts(), sessions=Sessions(Command()),
                                              identify=lambda: "c2"), HOME)
            for values in (dict(Active="no"), dict(name="work", uid="1001"), dict(Type="tty"), dict(Remote="yes")):
                with self.subTest(values=values), self.assertRaises(PermissionError):
                    authorized_caller(environment, accounts=Accounts(), sessions=Sessions(Command({"c2": properties(**values)})),
                                      identify=lambda: "c2")

    def test_native_library_does_not_use_forged_session_environment(self):
        with patch.dict(os.environ, {"XDG_SESSION_ID": "../../fake"}):
            try:
                value = process_session()
            except PermissionError:
                return  # Native containers have no logind session at all.
            self.assertNotEqual(value, "../../fake")


class LoginProcessTest(unittest.TestCase):
    def invoke(self, code, timeout=1):
        real = subprocess.Popen
        def start(command, **options):
            self.assertEqual(command[0], "/usr/bin/loginctl")
            self.assertEqual(options["env"], {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"})
            self.assertTrue(options["close_fds"])
            return real([sys.executable, "-I", "-c", code], **options)
        with patch("karyo_profiles.logind.subprocess.Popen", side_effect=start):
            return LoginCommand().run("list-sessions", timeout=timeout)

    def test_real_process_pipe_success_and_redacted_failure(self):
        self.assertEqual(self.invoke("print('[]')"), "[]\n")
        with self.assertRaisesRegex(SessionError, "could not read") as failure:
            self.invoke("import sys; print('secret detail', file=sys.stderr); sys.exit(1)")
        self.assertNotIn("secret", str(failure.exception))

    def test_actual_hanging_child_is_killed_at_deadline(self):
        started = time.monotonic()
        with self.assertRaises(TimeoutError):
            self.invoke("import time; time.sleep(30)", timeout=0.1)
        self.assertLess(time.monotonic() - started, 2)

    def test_actual_oversized_response_is_refused(self):
        with self.assertRaisesRegex(SessionError, "exceeds"):
            self.invoke("import sys; sys.stdout.write('x' * (512 * 1024)); sys.stdout.flush()")


if __name__ == "__main__":
    unittest.main()
