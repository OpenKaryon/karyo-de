#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed-host acceptance fixture for two real LightDM Karyo user sessions."""
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import socket
import sys

if os.getuid() != 0 or os.geteuid() != 0 or socket.gethostname() not in {
        "karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
    raise SystemExit("Use only the disposable acceptance hosts as root")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccount, RootCommands
from karyo_profiles.logind import Sessions
from karyo_profiles.model import DesktopSettings, Ownership, root_file
from karyo_profiles.provision import AccountProvisioner, LocalBackend
from karyo_profiles.storage import ConfigStore, atomic_write, trusted_directory

MAIN = "karyoqa"
CHILD = "kq-switch-work"
STATE = Path("/var/lib/karyo-qa/profile-switch.json")
backend = LocalBackend()
store = ConfigStore()
owner = backend.accounts.account(MAIN)
assert owner.uid == 1001


def fingerprint(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def password_fingerprint(name):
    return hashlib.sha256(backend.accounts.password_hash(name).encode()).hexdigest()


def save(record):
    directory = trusted_directory(STATE.parent, create=True)
    try:
        atomic_write(directory, STATE.name, (json.dumps(record) + "\n").encode())
        os.chmod(STATE.name, 0o600, dir_fd=directory)
    finally:
        os.close(directory)


def processes(name, executable):
    uid = backend.accounts.account(name).uid
    found = []
    for path in Path("/proc").iterdir():
        if not path.name.isdecimal():
            continue
        try:
            if path.stat().st_uid == uid:
                args = (path / "cmdline").read_bytes().split(b"\0")
                if args and Path(os.fsdecode(args[0])).name == executable:
                    found.append(int(path.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return sorted(found)


def check(record):
    assert backend.accounts.account(MAIN) == LocalAccount(**record["owner"])
    assert password_fingerprint(MAIN) == record["owner_password"]
    assert all(fingerprint(path) == value for path, value in record["pam"].items())
    assert backend.accounts.account(CHILD) == LocalAccount(**record["child"])
    assert password_fingerprint(CHILD) == record["child_password"]
    assert backend.accounts.expiry_day(CHILD) is None
    home = Path(record["child"]["home"])
    assert home == Path("/home") / CHILD
    info = home.lstat()
    assert [info.st_dev, info.st_ino, info.st_uid] == record["home"]
    assert store.ownership() == Ownership.parse(MAIN + " " + CHILD + "\n")
    assert store.settings(CHILD) == DesktopSettings(password=record["protection"])
    assert not store.has_settings(MAIN)


action = sys.argv[1:]
if action in (["prepare"], ["arm"]):
    assert not STATE.exists() and not STATE.is_symlink()
    assert CHILD not in backend.names() and not backend.home_exists(CHILD)
    assert not store.has_settings(CHILD) and not store.has_settings(MAIN)
    assert not store.ownership().families
    canonical = Path("/etc/karyo/desktops.conf")
    assert not canonical.exists() and not canonical.is_symlink()
    assert not any(row.name == CHILD for row in Sessions().all())
    record = {"phase": "preparing", "owner": asdict(owner),
        "owner_password": password_fingerprint(MAIN),
        "pam": {str(p): fingerprint(p) for p in sorted(Path("/etc/pam.d").glob("common-*"))},
        "accounts": {str(p): fingerprint(p) for p in map(Path,
            ("/etc/passwd", "/etc/shadow", "/etc/group", "/etc/gshadow"))},
        "main_shell": processes(MAIN, "karyo-shell"), "protection": "none"}
    assert len(record["main_shell"]) == 1
    save(record)
    if action == ["arm"]:
        print("READY new child absent and original state captured; create it through the actual GUI")
        raise SystemExit(0)
    child = AccountProvisioner(store, backend).add(owner.uid, "Work QA", name=CHILD)
    info = Path(child.home).lstat()
    record.update(phase="prepared", child=asdict(child), home=[info.st_dev, info.st_ino, info.st_uid],
                  child_password=password_fingerprint(CHILD))
    save(record)
    check(record)
    assert record["child_password"] == record["owner_password"]
    print("PASS known new shared QA child activated; owner password and common PAM unchanged")
elif action == ["capture"]:
    record = json.loads(root_file(STATE))
    assert record["phase"] == "preparing"
    child = backend.accounts.account(CHILD)
    assert child.home == "/home/" + CHILD
    info = Path(child.home).lstat()
    import stat
    assert stat.S_ISDIR(info.st_mode) and info.st_uid == child.uid
    assert not backend.has_processes(child.uid)
    record.update(phase="prepared", child=asdict(child), home=[info.st_dev, info.st_ino, info.st_uid],
                  child_password=password_fingerprint(CHILD))
    assert record["child_password"] == record["owner_password"]
    check(record)
    save(record)
    print("PASS actual GUI created the absent-before shared child; original owner/PAM unchanged")
else:
    record = json.loads(root_file(STATE))
    assert record["phase"] in {"prepared", "protected"}
    check(record)
    if action == ["protect"]:
        assert record["protection"] == "none"
        with store.locked():
            check(record)
            store.save_settings(CHILD, DesktopSettings(password="home"))
        record.update(phase="protected", protection="home")
        save(record)
        check(record)
        print("PASS child now requires Home password at every entry; credentials unchanged")
    elif action == ["status"]:
        rows = Sessions().all()
        family = [row for row in rows if row.name in {MAIN, CHILD}]
        assert len(family) == 2 and len({row.vt for row in family}) == 2
        assert sum(row.active for row in family) == 1
        assert processes(MAIN, "karyo-shell") == record["main_shell"]
        child_shell = processes(CHILD, "karyo-shell")
        assert len(child_shell) == 1
        if "child_shell" in record:
            assert child_shell == record["child_shell"]
        else:
            record["child_shell"] = child_shell
            save(record)
        print(json.dumps({"sessions": [asdict(row) for row in family],
                          "main_shell": record["main_shell"], "child_shell": child_shell}))
    elif action == ["cleanup"]:
        assert not backend.has_processes(record["child"]["uid"])
        assert not any(row.name == CHILD for row in Sessions().all())
        with store.locked():
            check(record)
            store.remove_new_settings(CHILD, DesktopSettings(password=record["protection"]))
            directory = trusted_directory("/etc/karyo")
            try:
                assert root_file("desktops.conf", dir_fd=directory) == MAIN + " " + CHILD + "\n"
                os.unlink("desktops.conf", dir_fd=directory)
                os.fsync(directory)
            finally:
                os.close(directory)
            # Explicit QA teardown of an absent-before, fully captured new
            # identity/home, never a product rollback of an activated account.
            RootCommands().run("userdel", "--remove", "--", CHILD)
        assert backend.accounts.account(CHILD, missing_ok=True) is None and not backend.home_exists(CHILD)
        assert password_fingerprint(MAIN) == record["owner_password"]
        assert all(fingerprint(path) == value for path, value in record["pam"].items())
        assert all(fingerprint(path) == value for path, value in record["accounts"].items())
        STATE.unlink()
        print("PASS known QA child/home/metadata removed; original account and common PAM bytes restored")
    else:
        raise SystemExit("Use prepare, arm, capture, protect, status or cleanup")
