# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary per-session XDG visibility overlays from optional administrator metadata."""
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import stat
import sys
import tempfile

from .model import account_name, root_file
from .storage import ConfigStore, trusted_directory

POLICY = Path("/etc/karyo/application-roles.json")
ROLES = frozenset(("home", "work"))
ENTRY = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]{0,239}\.desktop")
HIDDEN = b"[Desktop Entry]\nType=Application\nName=Karyo application policy\nHidden=true\n"


def unique(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("Duplicate application-role field")
        value[key] = item
    return value


def desktop_id(value):
    if not isinstance(value, str) or ENTRY.fullmatch(value) is None:
        raise ValueError("Application roles require bounded desktop-file basenames")
    return value


def parse(text):
    value = json.loads(text, object_pairs_hook=unique)
    if (not isinstance(value, dict) or set(value) != {"version", "applications", "autostart"}
            or type(value["version"]) is not int or value["version"] != 1):
        raise ValueError("Unsupported application-role policy")
    for key in ("applications", "autostart"):
        entries = value[key]
        if not isinstance(entries, dict) or len(entries) > 256:
            raise ValueError("Too many application-role entries")
        for name, roles in entries.items():
            desktop_id(name)
            if (not isinstance(roles, list) or any(not isinstance(v, str) or v not in ROLES for v in roles)
                    or len(set(roles)) != len(roles)):
                raise ValueError("Application roles must select home and/or work")
    return value


def load(path=POLICY):
    directory = None
    try:
        directory = trusted_directory(path.parent)
        return parse(root_file(path.name, dir_fd=directory))
    except FileNotFoundError:
        return None
    finally:
        if directory is not None:
            os.close(directory)


def role_for(store, login):
    account_name(login)
    if store.settings(login).workstation:
        return "work"
    family = store.ownership().family(login)
    return "home" if any(store.settings(name).workstation for name in family) else None


def view(policy, role):
    if role not in ROLES:
        raise ValueError("Unknown application role")
    return {"role": role, **{key: sorted(name for name, roles in policy[key].items() if role not in roles)
                             for key in ("applications", "autostart")}}


def private_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    info = os.fstat(fd)
    if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700:
        os.close(fd)
        raise PermissionError("Application catalog needs its ordinary user's private runtime")
    return fd


def check_cache(path, plan):
    descriptor = private_directory(path)
    try:
        if set(os.listdir(descriptor)) != {"share", "config"}:
            raise ValueError("Unexpected application catalog contents")
    finally:
        os.close(descriptor)
    for folder, section in (("share", "applications"), ("config", "autostart")):
        parent = private_directory(path / folder)
        try:
            if os.listdir(parent) != [section]:
                raise ValueError("Unexpected application catalog directory")
        finally:
            os.close(parent)
        leaf = private_directory(path / folder / section)
        try:
            if set(os.listdir(leaf)) != set(plan[section]):
                raise ValueError("Application catalog entry inventory changed")
            for name in plan[section]:
                fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=leaf)
                try:
                    info = os.fstat(fd)
                    if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                            or stat.S_IMODE(info.st_mode) != 0o644 or info.st_nlink != 1
                            or os.read(fd, len(HIDDEN) + 1) != HIDDEN):
                        raise ValueError("Application catalog entry changed")
                finally:
                    os.close(fd)
        finally:
            os.close(leaf)


def publish(runtime, plan):
    if (set(plan) != {"role", "applications", "autostart"} or plan["role"] not in ROLES
            or any(not isinstance(plan[key], list) or len(plan[key]) > 256
                   for key in ("applications", "autostart"))):
        raise ValueError("Invalid application catalog view")
    for key in ("applications", "autostart"):
        for name in plan[key]:
            desktop_id(name)
        if plan[key] != sorted(set(plan[key])):
            raise ValueError("Application catalog views must be canonical")
    runtime = Path(runtime)
    fd = private_directory(runtime)
    os.close(fd)
    digest = hashlib.sha256(json.dumps(plan, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    target = runtime / ("karyo-applications-" + digest)
    if os.path.lexists(target):
        check_cache(target, plan)
        return target
    stage = Path(tempfile.mkdtemp(prefix=".karyo-applications-", dir=runtime))
    identity = stage.stat().st_dev, stage.stat().st_ino
    try:
        for folder, section in (("share", "applications"), ("config", "autostart")):
            parent = stage / folder
            parent.mkdir(mode=0o700)
            leaf = parent / section
            leaf.mkdir(mode=0o700)
            for name in plan[section]:
                child = os.open(leaf / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
                try:
                    os.fchmod(child, 0o644)
                    os.write(child, HIDDEN)
                    os.fsync(child)
                finally:
                    os.close(child)
        check_cache(stage, plan)
        native = ctypes.CDLL(None, use_errno=True)
        native.renameat2.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        native.renameat2.restype = ctypes.c_int
        result = native.renameat2(-100, os.fsencode(stage), -100, os.fsencode(target), 1)
        if result != 0:
            error = ctypes.get_errno()
            if error != errno.EEXIST:
                raise OSError(error, "Could not publish the application catalog")
            check_cache(target, plan)
        else:
            stage = None
        return target
    finally:
        if stage is not None:
            if (stage.stat().st_dev, stage.stat().st_ino) != identity or not shutil.rmtree.avoids_symlink_attacks:
                raise ValueError("Application catalog staging identity changed")
            shutil.rmtree(stage)


def apply(*, policy_path=POLICY, store=None):
    uid = os.getuid()
    if uid != os.geteuid() or os.getgid() != os.getegid() or not 1000 <= uid < 65534:
        raise PermissionError("Application catalogs require an ordinary desktop account")
    policy = load(policy_path)
    if policy is None:
        return None
    login = pwd.getpwuid(uid).pw_name
    role = role_for(ConfigStore() if store is None else store, login)
    if role is None:
        return None
    runtime = Path("/run/user") / str(uid)
    if os.environ.get("XDG_RUNTIME_DIR") != str(runtime):
        raise ValueError("Application catalog runtime does not match the real UID")
    return publish(runtime, view(policy, role))


def main():
    if sys.argv[1:]:
        print("Usage: karyo-application-catalog", file=sys.stderr)
        return 2
    try:
        path = apply()
        if path is not None:
            print(path)
        return 0
    except (OSError, RuntimeError, ValueError):
        print("karyo-application-catalog: profile application metadata is unavailable or changed.", file=sys.stderr)
        return 1
