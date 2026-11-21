#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Disposable QA startup/PAM/VT checks without account or password mutation."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

if os.geteuid() != 0 or socket.gethostname() not in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
    raise SystemExit("Use only the disposable native acceptance hosts as root")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.logind import LoginCommand, Sessions, session_id
from karyo_profiles.model import DesktopSettings, root_file
from karyo_profiles.storage import ConfigStore, atomic_write, trusted_directory

STATE = Path("/var/lib/karyo-qa/protected-startup.json")
USER = "karyoqa"
POLICY = DesktopSettings(password="separate")
store = ConfigStore()
account = LocalAccounts().account(USER)
assert account.uid == 1001
tracked = [Path("/etc/passwd"), Path("/etc/shadow"), Path("/etc/group"), *sorted(Path("/etc/pam.d").glob("common-*"))]


def save(record):
    directory = trusted_directory(STATE.parent)
    try:
        atomic_write(directory, STATE.name, (json.dumps(record) + "\n").encode())
        os.chmod(STATE.name, 0o600, dir_fd=directory)
    finally:
        os.close(directory)


def unchanged(record):
    assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in record["hashes"].items())


def processes():
    found = []
    for path in Path("/proc").iterdir():
        if not path.name.isdecimal():
            continue
        try:
            if path.stat().st_uid == account.uid:
                command = (path / "cmdline").read_bytes().split(b"\0")
                if command[0]:
                    found.append([value.decode("utf-8", "strict") for value in command if value])
        except (FileNotFoundError, ProcessLookupError):
            continue
    return found


def graphical(locked):
    sessions = [value for value in Sessions().all() if value.uid == account.uid]
    assert len(sessions) == 1 and sessions[0].active
    value = subprocess.check_output(["/usr/bin/loginctl", "show-session", sessions[0].id,
                                    "--property=LockedHint", "--value"], text=True, timeout=3).strip()
    assert value == ("yes" if locked else "no")
    return sessions[0].vt


action = sys.argv[1:]
if action == ["prepare"]:
    assert not STATE.exists() and not STATE.is_symlink()
    assert not any(Path(command[0]).name in {"labwc", "karyo-shell", "karyo-lock"} for command in processes())
    with store.locked():
        assert not store.has_settings(USER)
        save({"phase": "prepared", "hashes": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}})
        store.save_settings(USER, POLICY)
    print("PASS mandatory startup policy prepared without changing QA credentials")
elif action in (["locked"], ["unlocked"], ["failed"]):
    record = json.loads(root_file(STATE))
    unchanged(record)
    assert store.settings(USER) == POLICY
    found = processes()
    names = [Path(command[0]).name for command in found]
    if action == ["failed"]:
        assert not any(name in {"labwc", "karyo-shell", "karyo-lock", "karyo-files"} for name in names)
        subprocess.run(["/usr/bin/systemctl", "is-active", "lightdm"], check=True, timeout=3)
        record["phase"] = "failed-closed"
        print("PASS failed initial locker ended the session without starting desktop clients")
    else:
        locked = action == ["locked"]
        assert names.count("labwc") == 1
        assert any(Path(command[0]).name == "labwc" and "--startup-lock" in command for command in found)
        vt = graphical(locked)
        if locked:
            assert names.count("karyo-lock") == 1
            assert any(Path(command[0]).name == "karyo-lock" and "--on-start" in command for command in found)
            assert "karyo-shell" not in names and "karyo-files" not in names
        else:
            assert "karyo-lock" not in names and names.count("karyo-shell") == 1 and "karyo-files" in names
        record["phase"] = "locked" if locked else "unlocked"
        print("PASS " + record["phase"] + " initial session on VT " + str(vt))
    save(record)
elif action == ["roundtrip-vt"]:
    record = json.loads(root_file(STATE))
    assert record["phase"] in {"unlocked", "returned-locked"}
    unchanged(record)
    assert store.settings(USER) == POLICY
    inventory = Sessions()
    rows = [value for value in inventory.all() if value.uid == account.uid and value.active]
    assert len(rows) == 1 and rows[0].vt != 1
    current = rows[0]
    # This fixed-host fixture uses the empty login console, never another
    # logged-in console or graphical user. Include all logind session classes.
    command = LoginCommand()
    deadline = time.monotonic() + 5
    listed = json.loads(command.run("list-sessions", "--json=short", timeout=5))
    assert isinstance(listed, list) and len(listed) <= 256
    for row in listed:
        identifier = session_id(row["session"])
        text = command.run("show-session", identifier, "--all", "--property=VTNr", "--property=Seat",
                           timeout=deadline - time.monotonic())
        lines = text.splitlines()
        assert len(lines) == 2
        properties = dict(line.split("=", 1) for line in lines)
        assert set(properties) == {"VTNr", "Seat"}
        assert not (properties["Seat"] == "seat0" and properties["VTNr"] == "1")

    def locked_hint():
        value = command.run("show-session", current.id, "--property=LockedHint", "--value", timeout=2).strip()
        assert value in {"yes", "no"}
        return value == "yes"

    descriptor = os.open("/dev/tty0", os.O_RDWR | os.O_CLOEXEC | os.O_NOCTTY)
    try:
        try:
            # Linux UAPI VT_ACTIVATE; no dependency on the optional kbd/chvt.
            fcntl.ioctl(descriptor, 0x5606, 1)
            deadline = time.monotonic() + 6
            while True:
                observed = inventory.get(current.id)
                assert observed is not None and observed.uid == account.uid and observed.vt == current.vt
                if not observed.active and locked_hint():
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError("The inactive QA desktop did not confirm its lock")
                time.sleep(0.05)
        finally:
            fcntl.ioctl(descriptor, 0x5606, current.vt)
    finally:
        os.close(descriptor)
    deadline = time.monotonic() + 4
    while not inventory.get(current.id).active:
        if time.monotonic() >= deadline:
            raise TimeoutError("The original QA graphical VT did not reactivate")
        time.sleep(0.05)
    assert locked_hint()
    record["phase"] = "returned-locked"
    save(record)
    print("READY locked VT restored; inspect the actual password form and verify PAM before accepting")
elif action in (["finish"], ["abort"]):
    record = json.loads(root_file(STATE))
    if action == ["finish"]:
        assert record["phase"] == "unlocked"
    unchanged(record)
    with store.locked():
        if store.has_settings(USER):
            store.remove_new_settings(USER, POLICY)
        else:
            assert action == ["abort"]
    STATE.unlink()
    print(("PASS" if action == ["finish"] else "ABORT") + " known startup metadata removed; original account and PAM bytes unchanged")
else:
    raise SystemExit("Use prepare, locked, unlocked, failed, roundtrip-vt, finish or abort")
