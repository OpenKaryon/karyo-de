#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual installed helpers acceptance; only two disposable QA hosts may run it."""
import ctypes
import hashlib
import hmac
import json
import os
from pathlib import Path
import socket
import sys

if os.geteuid() != 0 or socket.gethostname() not in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
    raise SystemExit("Run only as root on the disposable Karyo QA guests")
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import RootCommands
from karyo_profiles.entrypoints import read_password
from karyo_profiles.model import DesktopSettings, Ownership, root_file
from karyo_profiles.provision import LocalBackend
from karyo_profiles.storage import ConfigStore, atomic_write, trusted_directory

NAMES = ("kq-installed-shared", "kq-installed-separate")
STATE = Path("/var/lib/karyo-qa/profile-accounts-installed.json")
backend, store = LocalBackend(), ConfigStore()
owner = backend.accounts.account("karyoqa")
digest = hashlib.sha256(backend.accounts.password_hash(owner.name).encode()).hexdigest()
action = sys.argv[1:]
if action == ["prepare"]:
    assert not STATE.exists(), "An earlier fixture needs review"
    # This acceptance owns no pre-existing profile configuration.
    assert not (store.configuration / "desktops.conf").exists()
    assert not (store.legacy / "desktops.conf").exists()
    for name in NAMES:
        assert name not in backend.names() and not backend.home_exists(name) and not store.has_settings(name)
    directory = trusted_directory(STATE.parent)
    try:
        atomic_write(directory, STATE.name, (json.dumps({"owner_hash_digest": digest, "names_absent": NAMES}) + "\n").encode())
    finally:
        os.close(directory)
    print("PASS pre-existing accounts, homes and profile configuration are absent; owner digest recorded")
    raise SystemExit(0)
if action != ["verify-and-clean"]:
    raise SystemExit("Use prepare or verify-and-clean")
state = json.loads(root_file(STATE))
assert tuple(state["names_absent"]) == NAMES and state["owner_hash_digest"] == digest
password = read_password()
with store.locked():
    assert store.ownership() == Ownership(((owner.name, *NAMES),))
    captured = []
    for name in NAMES:
        account = backend.accounts.account(name)
        assert account.home == "/home/" + name and not backend.has_processes(account.uid)
        info = Path(account.home).lstat()
        assert info.st_uid == account.uid
        captured.append((account, info.st_dev, info.st_ino))
        assert backend.accounts.expiry_day(name) is None
    assert store.settings(NAMES[0]) == DesktopSettings()
    assert backend.accounts.password_hash(NAMES[0]) == backend.accounts.password_hash(owner.name)
    assert store.settings(NAMES[1]) == DesktopSettings(password="separate")
    encrypted = backend.accounts.password_hash(NAMES[1])
    crypt = ctypes.CDLL("libcrypt.so.1").crypt
    crypt.argtypes = (ctypes.c_char_p, ctypes.c_char_p)
    crypt.restype = ctypes.c_char_p
    verified = crypt(password.encode(), encrypted.encode())
    assert verified and hmac.compare_digest(verified, encrypted.encode())
    print("PASS installed polkit helper created shared and native YESCRYPT separate child accounts")
    # Explicit QA teardown, never a production rollback of usable accounts.
    for account, device, inode in reversed(captured):
        assert backend.accounts.account(account.name) == account
        info = Path(account.home).lstat()
        assert (info.st_dev, info.st_ino, info.st_uid) == (device, inode, account.uid)
        assert not backend.has_processes(account.uid)
        store.remove_new_settings(account.name, DesktopSettings(password="separate" if account.name == NAMES[1] else "none"))
        RootCommands().run("userdel", "--remove", "--", account.name)
    assert store.ownership() == Ownership(((owner.name, *NAMES),))
    directory = trusted_directory(store.configuration)
    try:
        os.unlink("desktops.conf", dir_fd=directory)
        os.fsync(directory)
    finally:
        os.close(directory)
assert hashlib.sha256(backend.accounts.password_hash(owner.name).encode()).hexdigest() == state["owner_hash_digest"]
for name in NAMES:
    assert backend.accounts.account(name, missing_ok=True) is None and not backend.home_exists(name)
STATE.unlink()
print("PASS installed fixture users/homes/metadata removed; main account credentials unchanged")
