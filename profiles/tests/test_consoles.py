# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import stat
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.consoles import ConsoleState, KernelConsoles, reserve_console
from karyo_profiles.logind import SessionError, Sessions


class InventoryCommand:
    def __init__(self, rows=None):
        self.rows = rows if rows is not None else {"gui": "8", "greeter": "9", "closing": "12", "manager": "0"}
        self.calls = []

    def run(self, *args, timeout):
        self.calls.append((args, timeout))
        if timeout <= 0:
            raise TimeoutError("fixture inventory deadline")
        if args[0] == "list-sessions":
            return json.dumps([{"session": name, "class": name} for name in self.rows])
        assert args[0] == "show-session" and args[2:] == ("--all", "--property=Id", "--property=VTNr")
        return "Id=" + args[1] + "\nVTNr=" + self.rows[args[1]] + "\n"


class FakeKernel:
    def __init__(self, busy=(8,)):
        self.active = 8
        self.busy = set(busy)
        self.events = []
        self.close_count = 0

    def __enter__(self):
        self.events.append("enter")
        return self

    def __exit__(self, *unused):
        self.events.append("exit")

    def state(self):
        return ConsoleState(self.active, frozenset(self.busy))

    def open(self, vt):
        assert vt not in self.busy
        self.events.append(("open", vt))
        self.busy.add(vt)
        return 100 + vt

    def close(self, fd):
        self.events.append(("close", fd))
        self.close_count += 1
        self.busy.remove(fd - 100)


class ConsoleTest(unittest.TestCase):
    def test_inventory_counts_all_classes_and_closing_owners(self):
        command = InventoryCommand()
        self.assertEqual(Sessions(command).occupied_vts(), frozenset({8, 9, 12}))
        self.assertEqual(len(command.calls), 5)
        self.assertTrue(all(0 < timeout <= 5 for _, timeout in command.calls))

    def test_inventory_rejects_ambiguous_missing_and_invalid_properties(self):
        for text in ("Id=other\nVTNr=8\n", "Id=gui\n", "Id=gui\nVTNr=8\nVTNr=9\n",
                     "Id=gui\nVTNr=8\nUnknown=1\n"):
            command = InventoryCommand({"gui": "8"})
            original = command.run
            def run(*args, timeout):
                return original(*args, timeout=timeout) if args[0] == "list-sessions" else text
            command.run = run
            with self.subTest(text=text), self.assertRaises(ValueError):
                Sessions(command).occupied_vts()
        for number in ("", "00", "-1", "64", "１２", "8x"):
            with self.subTest(number=number), self.assertRaises(ValueError):
                Sessions(InventoryCommand({"gui": number})).occupied_vts()

    def test_inventory_has_one_total_deadline_and_does_not_hide_lookup_failure(self):
        command = InventoryCommand({"first": "8", "second": "9"})
        with self.assertRaises(TimeoutError):
            Sessions(command, clock=iter((10, 10, 14, 16)).__next__).occupied_vts()
        self.assertEqual(command.calls[1][1], 1)
        with patch.object(InventoryCommand, "run", side_effect=SessionError("unavailable")):
            with self.assertRaises(SessionError):
                Sessions(InventoryCommand()).occupied_vts()

    def test_linux_state_bit_numbers_and_unrepresentable_active_console(self):
        self.assertEqual(ConsoleState.parse(8, 1 | (1 << 8) | (1 << 6)).busy, frozenset({6, 8}))
        self.assertEqual(ConsoleState.parse(27, 1).busy, frozenset({27}))
        for active, mask in ((0, 1), (64, 1), (True, 1), (8, 0), (8, -1), (8, 65536), (8, True)):
            with self.subTest(active=active, mask=mask), self.assertRaises(SessionError):
                ConsoleState.parse(active, mask)

    def test_no_lock_means_no_kernel_or_inventory_access(self):
        kernel = FakeKernel()
        with self.assertRaises(PermissionError):
            with reserve_console(SimpleNamespace(_locked=False), kernel=kernel):
                self.fail("unlocked reservation")
        self.assertEqual(kernel.events, [])

    def test_pool_selection_uses_kernel_and_all_session_owners(self):
        kernel = FakeKernel({8, 10})
        with reserve_console(SimpleNamespace(_locked=True), Sessions(InventoryCommand({"greeter": "9"})), kernel) as lease:
            self.assertEqual(lease.vt, 11)
            self.assertEqual(lease.path, "/dev/tty11")
            self.assertIn(11, kernel.busy)
            lease.verify()
        self.assertEqual(kernel.events, ["enter", ("open", 11), ("close", 111), "exit"])
        self.assertEqual(kernel.busy, {8, 10})

    def test_closed_lease_or_released_configuration_lock_cannot_be_reused(self):
        kernel = FakeKernel()
        store = SimpleNamespace(_locked=True)
        with reserve_console(store, Sessions(InventoryCommand({})), kernel) as lease:
            store._locked = False
            with self.assertRaises(PermissionError):
                lease.verify()
            store._locked = True
        with self.assertRaises(PermissionError):
            lease.verify()
        self.assertEqual(kernel.close_count, 1)

    def test_full_pool_never_opens_or_displaces_a_console(self):
        kernel = FakeKernel({8, 9, 10, 11, 12})
        with self.assertRaises(SessionError):
            with reserve_console(SimpleNamespace(_locked=True), Sessions(InventoryCommand({})), kernel):
                self.fail("all consoles occupied")
        self.assertEqual(kernel.events, ["enter", "exit"])

    def test_new_owner_after_open_aborts_and_closes_only_our_descriptor(self):
        kernel = FakeKernel()
        sessions = SimpleNamespace(occupied_vts=iter((frozenset(), frozenset({9}))).__next__)
        with self.assertRaises(SessionError):
            with reserve_console(SimpleNamespace(_locked=True), sessions, kernel):
                self.fail("a new owner appeared")
        self.assertEqual(kernel.events, ["enter", ("open", 9), ("close", 109), "exit"])

    def test_activation_after_reservation_or_lost_busy_state_fails_verification(self):
        for change in (lambda kernel: setattr(kernel, "active", 9), lambda kernel: kernel.busy.remove(9)):
            kernel = FakeKernel()
            # A lost-busy fake still must accept closing the held descriptor.
            kernel.close = lambda fd: kernel.events.append(("close", fd))
            with self.subTest(change=change), self.assertRaises(SessionError):
                with reserve_console(SimpleNamespace(_locked=True), Sessions(InventoryCommand({})), kernel) as lease:
                    change(kernel)
                    lease.verify()
            self.assertEqual(kernel.events[-2:], [("close", 109), "exit"])

    def test_caller_failure_releases_lease_without_activation_or_reset(self):
        kernel = FakeKernel()
        with self.assertRaisesRegex(RuntimeError, "caller failed"):
            with reserve_console(SimpleNamespace(_locked=True), Sessions(InventoryCommand({})), kernel):
                raise RuntimeError("caller failed")
        self.assertEqual(kernel.close_count, 1)
        self.assertEqual(kernel.active, 8)
        self.assertEqual(kernel.busy, {8})

    def test_native_console_controller_refuses_ordinary_role_before_open(self):
        with patch("os.getuid", return_value=1000), patch("os.geteuid", return_value=1000), patch("os.open") as opened:
            with self.assertRaises(PermissionError):
                with KernelConsoles():
                    self.fail("ordinary role")
            opened.assert_not_called()

    def test_native_device_paths_cannot_be_symlinks_regular_files_or_other_devices(self):
        for mode, device in ((stat.S_IFREG, os.makedev(4, 9)), (stat.S_IFCHR, os.makedev(4, 8)),
                             (stat.S_IFCHR, os.makedev(5, 9))):
            with self.subTest(mode=mode, device=device), patch("os.open", return_value=57) as opened, \
                    patch("os.fstat", return_value=SimpleNamespace(st_mode=mode, st_rdev=device)), patch("os.close") as closed:
                with self.assertRaises(SessionError):
                    KernelConsoles._open(9)
                self.assertEqual(opened.call_args.args[0], "/dev/tty9")
                flags = opened.call_args.args[1]
                self.assertEqual(flags & (os.O_NOFOLLOW | os.O_NOCTTY | os.O_CLOEXEC),
                                 os.O_NOFOLLOW | os.O_NOCTTY | os.O_CLOEXEC)
                closed.assert_called_once_with(57)

    def test_native_state_uses_fixed_ioctl_and_does_not_switch_console(self):
        kernel = KernelConsoles()
        kernel.control = 57
        def query(fd, request, values, mutate):
            self.assertEqual((fd, request, mutate), (57, 0x5603, True))
            values[0], values[2] = 8, 257
        with patch("fcntl.ioctl", side_effect=query) as ioctl:
            self.assertEqual(kernel.state(), ConsoleState(8, frozenset({8})))
            self.assertEqual(ioctl.call_count, 1)
        for vt in (0, 7, 13, 63, True, "9"):
            with self.subTest(vt=vt), self.assertRaises(ValueError):
                kernel.open(vt)


if __name__ == "__main__":
    unittest.main()
