#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Metadata-only startup acceptance restricted to the disposable QA machines."""
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys

if os.geteuid() != 0 or socket.gethostname() not in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
    raise SystemExit("Use only the disposable native acceptance hosts as root")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.logind import Sessions
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
    raise SystemExit("Use prepare, locked, unlocked, failed, finish or abort")
