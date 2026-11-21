#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real account lifecycle, ONLY on the two disposable Karyo QA guests."""
import ctypes
import grp
import hmac
import os
from pathlib import Path
import socket
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import RootCommands
from karyo_profiles.model import Ownership
from karyo_profiles.provision import AccountProvisioner, CreationError, LocalBackend
from karyo_profiles.storage import ConfigStore, trusted_directory

NAMES = ("kq-native-shared", "kq-native-separate", "kq-native-rollback", "kq-native-retained")


def main():
    if os.geteuid() != 0 or socket.gethostname() not in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
        raise SystemExit("This test is restricted to the disposable acceptance VMs")
    raw = sys.stdin.buffer.readline(1026)
    if not raw.endswith(b"\n") or not 8 <= len(raw) - 1 <= 1024 or any(c in raw[:-1] for c in b"\0\r\n"):
        raise SystemExit("Supply a disposable fixture password on stdin")
    password = raw[:-1].decode("utf-8")
    backend = LocalBackend()
    owner = backend.accounts.account("karyoqa")
    before_hash = backend.accounts.password_hash(owner.name)
    for name in NAMES:
        if name in backend.names() or backend.home_exists(name):
            raise SystemExit("An acceptance name or home already exists; nothing will be deleted")
    directory = trusted_directory("/var/lib/karyo-qa", create=True)
    os.close(directory)
    root = Path(tempfile.mkdtemp(prefix="profile-provision-", dir="/var/lib/karyo-qa"))
    store = ConfigStore(root / "etc/karyo", root / "etc/legacy", root / "run")
    with store.locked():
        store.save_ownership(Ownership.parse(owner.name + "\n"))
    provision = AccountProvisioner(store, backend)
    captured = []

    def remember(account):
        info = Path(account.home).lstat()
        captured.append((account, info.st_dev, info.st_ino))

    try:
        shared = provision.add(owner.uid, "Shared QA", name=NAMES[0], protection="home")
        remember(shared)
        assert backend.accounts.password_hash(shared.name) == before_hash
        assert backend.accounts.expiry_day(shared.name) is None
        assert store.ownership().owner(shared.name) == owner.name
        inherited = set(backend.inherited_groups(owner))
        actual_groups = set(os.getgrouplist(shared.name, shared.gid))
        assert {grp.getgrnam(group).gr_gid for group in inherited}.issubset(actual_groups)
        sudo = grp.getgrnam("sudo").gr_gid
        assert (sudo in actual_groups) == ("sudo" in inherited)
        print("PASS native shared child, inherited groups and final account activation", flush=True)

        separate = provision.add(shared.uid, "Separate QA", name=NAMES[1],
                                 protection="separate", password=password)
        remember(separate)
        encrypted = backend.accounts.password_hash(separate.name)
        crypt = ctypes.CDLL("libcrypt.so.1").crypt
        crypt.argtypes = (ctypes.c_char_p, ctypes.c_char_p)
        crypt.restype = ctypes.c_char_p
        verified = crypt(password.encode("utf-8"), encrypted.encode("utf-8"))
        assert verified and hmac.compare_digest(verified, encrypted.encode("utf-8"))
        assert store.settings(separate.name).password == "separate"
        assert backend.accounts.expiry_day(separate.name) is None
        assert backend.accounts.password_hash(owner.name) == before_hash
        print("PASS child creates sibling with native YESCRYPT separate password and unchanged owner", flush=True)

        before = store.ownership()
        with patch.object(store, "save_ownership", side_effect=OSError("public QA failure before publication")):
            try:
                provision.add(owner.uid, "Rollback QA", name=NAMES[2])
                raise AssertionError("Native rollback fixture did not fail")
            except CreationError as error:
                assert not error.retained
        assert backend.accounts.account(NAMES[2], missing_ok=True) is None
        assert not backend.home_exists(NAMES[2])
        assert not store.has_settings(NAMES[2])
        assert store.ownership() == before
        print("PASS native failed preparation removes only its fresh expired account/home", flush=True)

        class FailingAfterActivation(LocalBackend):
            def activate(self, *args):
                super().activate(*args)
                raise OSError("public QA failure after actual activation")

        retained_backend = FailingAfterActivation()
        try:
            AccountProvisioner(store, retained_backend).add(owner.uid, "Retained QA", name=NAMES[3])
            raise AssertionError("Native retention fixture did not fail")
        except CreationError as error:
            assert error.retained
        retained = backend.accounts.account(NAMES[3])
        remember(retained)
        assert backend.accounts.expiry_day(retained.name) is None
        assert store.ownership().owner(retained.name) == owner.name
        print("PASS already activated native child is retained instead of automatically deleted", flush=True)
        assert backend.accounts.password_hash(owner.name) == before_hash
    finally:
        # Explicit test teardown, not the product rollback policy. Each entry
        # was absent before this fixture and its complete identity was captured.
        for account, device, inode in reversed(captured):
            assert account.name in NAMES and account.name.startswith("kq-native-")
            assert backend.accounts.account(account.name) == account
            assert Path(account.home) == Path("/home") / account.name
            info = Path(account.home).lstat()
            assert (info.st_dev, info.st_ino, info.st_uid) == (device, inode, account.uid)
            assert not backend.has_processes(account.uid)
            RootCommands().run("userdel", "--remove", "--", account.name)
        assert backend.accounts.password_hash(owner.name) == before_hash
    for name in NAMES:
        assert backend.accounts.account(name, missing_ok=True) is None and not backend.home_exists(name)
    print("PASS fixture users/homes removed, owner unchanged; metadata evidence:", root, flush=True)


if __name__ == "__main__":
    main()
