#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native installed password hook acceptance using only fresh QA accounts."""
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import sys

if os.getuid() != 0 or os.geteuid() != 0 or socket.gethostname() not in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
    raise SystemExit("Run only as root on the two disposable QA guests")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import RootCommands
from karyo_profiles.entrypoints import read_password
from karyo_profiles.model import DesktopSettings, Ownership
from karyo_profiles.provision import AccountProvisioner, LocalBackend
from karyo_profiles.storage import ConfigStore, atomic_write, trusted_directory

NAMES = ("kq-pam-owner", "kq-pam-shared", "kq-pam-separate")
PROBE = "/usr/local/libexec/karyo-qa-pam-password"
PAM = Path("/etc/pam.d/karyo-qa-password")
PAM_DATA = ("password requisite pam_unix.so yescrypt\n"
            "password required pam_exec.so seteuid /usr/libexec/karyo/desktop-password-changed\n")
global_stack = sys.argv[1:] == ["--global-stack"]
if sys.argv[1:] not in ([], ["--global-stack"]):
    raise SystemExit("Only the private stack or --global-stack is supported")
if global_stack:
    PAM_DATA = "@include common-password\n"
    assert "/usr/libexec/karyo/desktop-password-changed" in Path("/etc/pam.d/common-password").read_text()
password = read_password()
backend, store = LocalBackend(), ConfigStore()
tracked = (Path("/etc/passwd"), Path("/etc/shadow"), Path("/etc/group"), Path("/etc/pam.d/common-password"))
before = {path: hashlib.sha256(path.read_bytes()).digest() for path in tracked}
assert not PAM.exists() and not PAM.is_symlink()
assert not (store.configuration / "desktops.conf").exists()
assert not (store.legacy / "desktops.conf").exists()
for name in NAMES:
    assert name not in backend.names() and not backend.home_exists(name) and not store.has_settings(name)
captured = []
pam_created = False

def remember(account):
    info = Path(account.home).lstat()
    captured.append((account, info.st_dev, info.st_ino))


def secret(suffix):
    # Global obscure policy gets unrelated fixture passwords, not near matches.
    return hashlib.sha256((password + suffix).encode()).hexdigest() if global_stack else password + suffix


def update(name, suffix):
    result = subprocess.run([PROBE, name], input=secret(suffix) + "\n", text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True, timeout=30,
        env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"})
    if result.returncode:
        raise RuntimeError("The native private PAM fixture did not finish successfully")


try:
    owner_pending = backend.create(NAMES[0], "PAM QA owner", ())
    owner = owner_pending.account
    remember(owner)
    backend.activate(owner_pending, DesktopSettings(password="separate"), password, "!")
    provision = AccountProvisioner(store, backend)
    shared = provision.add(owner.uid, "PAM QA shared", name=NAMES[1])
    remember(shared)
    separate = provision.add(owner.uid, "PAM QA separate", name=NAMES[2], protection="separate", password=password)
    remember(separate)
    assert store.ownership() == Ownership(((owner.name, shared.name, separate.name),))
    directory = trusted_directory(PAM.parent)
    try:
        atomic_write(directory, PAM.name, PAM_DATA.encode())
        pam_created = True
    finally:
        os.close(directory)
    separate_before = backend.accounts.password_hash(separate.name)
    update(owner.name, "-owner-change")
    owner_hash = backend.accounts.password_hash(owner.name)
    assert owner_hash == backend.accounts.password_hash(shared.name)
    assert backend.accounts.password_hash(separate.name) == separate_before
    print("PASS installed PAM hook propagates main-account change to shared child only", flush=True)
    update(shared.name, "-shared-change")
    shared_hash = backend.accounts.password_hash(shared.name)
    assert shared_hash != owner_hash and shared_hash == backend.accounts.password_hash(owner.name)
    assert backend.accounts.password_hash(separate.name) == separate_before
    print("PASS installed PAM hook propagates shared-child change back to main account", flush=True)
    update(separate.name, "-separate-change")
    assert backend.accounts.password_hash(separate.name) != separate_before
    assert backend.accounts.password_hash(owner.name) == shared_hash == backend.accounts.password_hash(shared.name)
    print("PASS installed PAM hook leaves a separate child independent", flush=True)
    before_denied = {name: backend.accounts.password_hash(name) for name in NAMES}
    user_environment = {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"}
    denied = subprocess.run([PROBE, shared.name, "--as-fixture-user"],
        input="DefinitelyWrongFixturePassword\n" + secret("-must-not-apply") + "\n", text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True, timeout=30, env=user_environment)
    assert denied.returncode == 1
    assert {name: backend.accounts.password_hash(name) for name in NAMES} == before_denied
    print("PASS ordinary real-UID PAM client rejects a wrong current password without family changes", flush=True)
    changed = subprocess.run([PROBE, shared.name, "--as-fixture-user"],
        input=secret("-shared-change") + "\n" + secret("-ordinary-change") + "\n", text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True, timeout=30, env=user_environment)
    if changed.returncode:
        raise RuntimeError("Ordinary real-UID native PAM password update did not finish")
    ordinary_hash = backend.accounts.password_hash(shared.name)
    assert ordinary_hash != shared_hash and ordinary_hash == backend.accounts.password_hash(owner.name)
    assert backend.accounts.password_hash(separate.name) == before_denied[separate.name]
    shared_hash = ordinary_hash
    print("PASS ordinary real-UID PAM client verifies current password and updates shared family", flush=True)
    if global_stack:
        root_new = secret("-actual-passwd-owner")
        changed = subprocess.run(["/usr/bin/passwd", owner.name], input=root_new + "\n" + root_new + "\n",
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True, timeout=30, env=user_environment)
        if changed.returncode:
            raise RuntimeError("Actual administrative passwd did not finish")
        shared_hash = backend.accounts.password_hash(owner.name)
        assert shared_hash == backend.accounts.password_hash(shared.name)
        assert backend.accounts.password_hash(separate.name) == before_denied[separate.name]
        print("PASS actual distro passwd updates the main account and its shared child", flush=True)
        snapshots = {name: backend.accounts.password_hash(name) for name in NAMES}
        denied = subprocess.run(["/usr/sbin/runuser", "-u", shared.name, "--", "/usr/bin/passwd"],
            input="DefinitelyWrongFixturePassword\n", text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, close_fds=True, timeout=30, env=user_environment)
        assert denied.returncode != 0
        assert {name: backend.accounts.password_hash(name) for name in NAMES} == snapshots
        user_new = secret("-actual-passwd-shared")
        changed = subprocess.run(["/usr/sbin/runuser", "-u", shared.name, "--", "/usr/bin/passwd"],
            input=root_new + "\n" + user_new + "\n" + user_new + "\n", text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True, timeout=30, env=user_environment)
        if changed.returncode:
            raise RuntimeError("Actual ordinary-user passwd did not finish")
        shared_hash = backend.accounts.password_hash(shared.name)
        assert shared_hash != snapshots[shared.name] and shared_hash == backend.accounts.password_hash(owner.name)
        assert backend.accounts.password_hash(separate.name) == snapshots[separate.name]
        print("PASS actual ordinary-user passwd rejects wrong current password and synchronizes only shared accounts", flush=True)
    subprocess.run(["/usr/libexec/karyo/desktop-sync"], check=True, timeout=20,
                   env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"})
    assert backend.accounts.password_hash(owner.name) == shared_hash == backend.accounts.password_hash(shared.name)
finally:
    if pam_created:
        assert PAM.read_text() == PAM_DATA
        PAM.unlink()
    # Teardown is allowed only for complete new identities captured by this test.
    expected = Ownership(((NAMES[0], *(entry[0].name for entry in captured[1:])),)) if len(captured) > 1 else Ownership()
    with store.locked():
        assert store.ownership() == expected
        for account, device, inode in reversed(captured):
            assert account.name in NAMES and backend.accounts.account(account.name) == account
            assert Path(account.home) == Path("/home") / account.name
            info = Path(account.home).lstat()
            assert (info.st_dev, info.st_ino, info.st_uid) == (device, inode, account.uid)
            assert not backend.has_processes(account.uid)
            if store.has_settings(account.name):
                store.remove_new_settings(account.name, DesktopSettings(password="separate" if account.name == NAMES[2] else "none"))
            RootCommands().run("userdel", "--remove", "--", account.name)
        configuration = store.configuration / "desktops.conf"
        if configuration.exists():
            directory = trusted_directory(store.configuration)
            try:
                os.unlink(configuration.name, dir_fd=directory)
                os.fsync(directory)
            finally:
                os.close(directory)
for path in tracked:
    assert hashlib.sha256(path.read_bytes()).digest() == before[path]
for name in NAMES:
    assert backend.accounts.account(name, missing_ok=True) is None and not backend.home_exists(name)
print("PASS temporary PAM service and new users/homes/metadata removed; original account/PAM bytes unchanged", flush=True)
