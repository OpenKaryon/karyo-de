#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Metadata-only acceptance fixture restricted to disposable native QA guests."""
import hashlib
import json
import os
from pathlib import Path
import socket
import sys

if os.geteuid() != 0 or socket.gethostname() not in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
    raise SystemExit("Run only as root on the disposable acceptance guests")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.model import DesktopSettings, root_file
from karyo_profiles.storage import ConfigStore, atomic_write, trusted_directory

STATE = Path("/var/lib/karyo-qa/ui-policy-native.json")
MISSING = "/etc/karyo/ui-policy-qa-missing.conf"
USER = "karyoqa"
SETTINGS = DesktopSettings(password="separate")
LEGACY = "karyoqa\n"
store = ConfigStore()
account = LocalAccounts().account(USER)
assert account.uid == 1001
tracked = (Path("/etc/passwd"), Path("/etc/shadow"), Path("/etc/group"), Path("/etc/pam.d/common-password"))


def save(record):
    directory = trusted_directory(STATE.parent)
    try:
        atomic_write(directory, STATE.name, (json.dumps(record) + "\n").encode())
        os.chmod(STATE.name, 0o600, dir_fd=directory)
    finally:
        os.close(directory)


def unchanged(record):
    assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest
               for path, digest in record["digests"].items())


action = sys.argv[1:]
if action == ["prepare-settings"]:
    assert not STATE.exists() and not STATE.is_symlink()
    with store.locked():
        assert not store.has_settings(USER)
        assert not (store.configuration / "desktops.conf").exists() and not (store.configuration / "desktops.conf").is_symlink()
        assert not (store.legacy / "desktops.conf").exists() and not (store.legacy / "desktops.conf").is_symlink()
        record = {"phase": "preparing-settings", "legacy_directory_existed": store.legacy.exists(),
                  "digests": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in tracked}}
        save(record)
        store.save_settings(USER, SETTINGS)
        record["phase"] = "settings"
        save(record)
    print("PASS root protection fixture prepared; no account or password changed")
elif action == ["clean-settings"]:
    record = json.loads(root_file(STATE))
    assert record["phase"] == "settings"
    unchanged(record)
    with store.locked():
        store.remove_new_settings(USER, SETTINGS)
        record["phase"] = "settings-clean"
        save(record)
    print("PASS only the unchanged fresh protection fixture removed")
elif action == ["prepare-invalid-greeter"]:
    record = json.loads(root_file(STATE))
    assert record["phase"] == "settings-clean"
    unchanged(record)
    with store.locked():
        current = store.configuration / "desktops.conf"
        legacy = store.legacy / "desktops.conf"
        assert not current.exists() and not current.is_symlink() and not legacy.exists() and not legacy.is_symlink()
        assert not Path(MISSING).exists() and not Path(MISSING).is_symlink()
        directory = trusted_directory(store.legacy, create=True)
        try:
            atomic_write(directory, "desktops.conf", LEGACY.encode())
        finally:
            os.close(directory)
        directory = trusted_directory(store.configuration)
        try:
            os.symlink(MISSING, "desktops.conf", dir_fd=directory)
            os.fsync(directory)
        finally:
            os.close(directory)
        record["phase"] = "invalid-greeter"
        save(record)
    print("PASS controlled dangling canonical metadata and valid legacy fallback prepared")
elif action == ["clean-invalid-greeter"]:
    record = json.loads(root_file(STATE))
    assert record["phase"] == "invalid-greeter"
    unchanged(record)
    with store.locked():
        current = store.configuration / "desktops.conf"
        assert current.is_symlink() and current.lstat().st_uid == 0 and os.readlink(current) == MISSING
        assert root_file(store.legacy / "desktops.conf") == LEGACY
        for folder in (store.configuration, store.legacy):
            directory = trusted_directory(folder)
            try:
                os.unlink("desktops.conf", dir_fd=directory)
                os.fsync(directory)
            finally:
                os.close(directory)
        if not record["legacy_directory_existed"]:
            store.legacy.rmdir()  # Refuses to remove a directory containing unexpected data.
        record["phase"] = "metadata-clean"
        save(record)
    print("PASS only the known new symlink and unchanged legacy fixture removed")
elif action == ["finish"]:
    record = json.loads(root_file(STATE))
    assert record["phase"] == "metadata-clean"
    unchanged(record)
    assert not store.has_settings(USER)
    assert not (store.configuration / "desktops.conf").exists() and not (store.configuration / "desktops.conf").is_symlink()
    assert not (store.legacy / "desktops.conf").exists()
    STATE.unlink()
    print("PASS all metadata fixtures cleaned; account/password/PAM bytes unchanged")
else:
    raise SystemExit("Use prepare-settings, clean-settings, prepare-invalid-greeter, clean-invalid-greeter or finish")
