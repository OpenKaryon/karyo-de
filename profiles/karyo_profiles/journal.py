# SPDX-License-Identifier: GPL-3.0-or-later
"""Private write-ahead ownership journal for the host's first resource layer."""
from dataclasses import asdict
import fcntl
import json
import os
from pathlib import Path
import re
import stat
import uuid

from .credentials import LocalAccount
from .logind import session_id
from .model import account_name
from .networks import NetworkSpec
from .services import host_controller
from .storage import atomic_write, trusted_directory

LIMIT = 1024 * 1024
ADDRESS = Path("/run/karyo/profile-host")


class JournalRetained(RuntimeError):
    """Unproven or incomplete resources must remain untouched for review."""


def unique_object(pairs):
    values = {}
    for key, value in pairs:
        if key in values:
            raise ValueError("Duplicate host journal field")
        values[key] = value
    return values


def ordinary_uid(value, *, private=False):
    if type(value) is not int or not 1000 <= value < (17384 if private else 65534):
        raise ValueError("Invalid host journal desktop UID")


def validate_account(value):
    if not isinstance(value, dict) or set(value) != {"name", "uid", "gid", "home", "shell"}:
        raise ValueError("Invalid host journal account record")
    account_name(value["name"])
    ordinary_uid(value["uid"], private=True)
    if type(value["gid"]) is not int or not 0 <= value["gid"] < 2**32:
        raise ValueError("Invalid host journal account group")
    for name in ("home", "shell"):
        text = value[name]
        if (not isinstance(text, str) or len(text) > 4096 or not Path(text).is_absolute()
                or ".." in Path(text).parts or any(ord(c) < 32 or ord(c) == 127 for c in text)):
            raise ValueError("Invalid host journal account path")
    return LocalAccount(**value)


def validate_network(value):
    if not isinstance(value, dict) or set(value) != {"namespace", "indices", "macs"}:
        raise ValueError("Invalid host journal network receipt")
    for name in ("namespace", "indices"):
        values = value[name]
        if (not isinstance(values, list) or len(values) != 2
                or any(type(item) is not int or not 0 < item < 2**64 for item in values)):
            raise ValueError("Invalid host journal kernel identity")
    macs = value["macs"]
    if (not isinstance(macs, list) or len(macs) != 2 or any(not isinstance(item, str)
            or re.fullmatch("[0-9a-f]{2}(?::[0-9a-f]{2}){5}", item) is None for item in macs)):
        raise ValueError("Invalid host journal link identity")


def validate_record(value):
    base={"token","account","source","pending","network"}
    if (not isinstance(value,dict) or not base<=set(value) or set(value)-base-{"dns","session","forwarding"}
            or "session" in value and "dns" not in value):
        raise ValueError("Invalid host journal profile record")
    account = validate_account(value["account"])
    NetworkSpec(account.uid, value["token"])
    source = value["source"]
    if not isinstance(source, dict) or set(source) != {"id", "name", "uid", "vt"}:
        raise ValueError("Invalid host journal source login")
    session_id(source["id"]); account_name(source["name"]); ordinary_uid(source["uid"])
    if (source["name"] == account.name or source["uid"] == account.uid
            or type(source["vt"]) is not int or not 1 <= source["vt"] <= 63):
        raise ValueError("Invalid host journal source desktop")
    if value["pending"] is not None and (not isinstance(value["pending"], str) or value["pending"] not in {"create", "remove"}):
        raise ValueError("Invalid host journal mutation state")
    if value["network"] is None:
        if value["pending"] != "create":
            raise ValueError("Missing host journal resource receipt")
    else:
        if value["pending"] == "create":
            raise ValueError("Ambiguous host journal creation receipt")
        validate_network(value["network"])
    if "forwarding" in value:
        if value["pending"] is not None or value["network"] is None:
            raise ValueError("Forwarding requires its acknowledged network")
        from .journal_forwarding_record import validate_forwarding
        validate_forwarding(value["forwarding"],NetworkSpec(account.uid,value["token"]))
        if "dns" in value and value["forwarding"]["pending"] is not None:
            raise ValueError("DNS cannot coexist with a forwarding mutation")
    if "dns" in value:
        if value["pending"] is not None or value["network"] is None:
            raise ValueError("DNS requires an acknowledged live network receipt")
        from .journal_dns_record import validate_dns
        validate_dns(value["dns"], NetworkSpec(account.uid,value["token"]),account.gid)
        if "forwarding" in value and value["dns"]["seed"]["physical"]!=value["forwarding"]["physical"]:
            raise ValueError("Forwarding and DNS require the same physical seed")
    if "session" in value:
        if value["dns"]["pending"] is not None or value["dns"]["bus"] is None or value["dns"]["resolver"] is None:
            raise ValueError("A session requires acknowledged private DNS services")
        from .journal_session_record import validate_session
        validate_session(value["session"],account)


class RuntimeJournal:
    def __init__(self, path=ADDRESS, *, guard=host_controller):
        self.path, self.host_guard = Path(path), guard
        self.directory = self.lock = None
        self.directory_identity = self.lock_identity = self.file_identity = None
        self.data = self.raw = None
        self.known = set()
        self.pid = os.getpid()
        self.boot = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        if str(uuid.UUID(self.boot)) != self.boot:
            raise ValueError("Invalid host boot identity")
        try:
            self.host_guard()
            parent = trusted_directory(self.path.parent, create=True)
            try:
                try:
                    os.mkdir(self.path.name, 0o700, dir_fd=parent)
                    os.fsync(parent)
                except FileExistsError:
                    pass
                self.directory = os.open(self.path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
                                         | os.O_CLOEXEC, dir_fd=parent)
            finally:
                os.close(parent)
            info = os.fstat(self.directory)
            if info.st_uid != 0 or stat.S_IMODE(info.st_mode) != 0o700:
                raise PermissionError("Unsafe host journal directory")
            self.directory_identity = info.st_dev, info.st_ino
            self.lock = os.open("controller.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW
                | os.O_NONBLOCK | os.O_CLOEXEC, 0o600, dir_fd=self.directory)
            info = os.fstat(self.lock)
            self.private_file(info)
            self.lock_identity = info.st_dev, info.st_ino
            try:
                fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                raise JournalRetained("Another host controller owns the journal") from None
            if set(os.listdir(self.directory)) - {"controller.lock", "state.json"}:
                raise JournalRetained("Unexpected host journal objects require review")
            self.raw, self.file_identity = self.read()
            self.data = ({"schema": 1, "boot": self.boot, "lock": list(self.lock_identity), "profiles": []} if self.raw is None
                         else json.loads(self.raw, object_pairs_hook=unique_object))
            self.validate(self.data)
            if self.raw is None:
                self.save(self.data)
        except BaseException:
            self.close()
            raise

    @staticmethod
    def private_file(info):
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or stat.S_IMODE(info.st_mode) != 0o600
                or info.st_nlink != 1 or info.st_size > LIMIT):
            raise PermissionError("Unsafe host journal file")

    def validate(self, data):
        if (not isinstance(data, dict) or set(data) != {"schema", "boot", "lock", "profiles"}
                or type(data["schema"]) is not int or data["schema"] not in {1, 2, 3, 4} or data["boot"] != self.boot
                or data["lock"] != list(self.lock_identity)
                or not isinstance(data["profiles"], list) or len(data["profiles"]) > 64):
            raise JournalRetained("Unsupported or foreign-boot host journal requires review")
        names, uids, tokens = set(), set(), set()
        for record in data["profiles"]:
            validate_record(record)
            if "dns" in record and data["schema"] not in {2,3,4}:
                raise ValueError("DNS ownership requires journal schema 2 or later")
            if "session" in record and data["schema"] not in {3,4}:
                raise ValueError("Session ownership requires journal schema 3 or later")
            if "forwarding" in record and data["schema"]!=4:
                raise ValueError("Forwarding ownership requires journal schema 4")
            account = record["account"]
            if account["name"] in names or account["uid"] in uids or record["token"] in tokens:
                raise ValueError("Duplicate host journal profile identity")
            names.add(account["name"]); uids.add(account["uid"]); tokens.add(record["token"])

    def guard(self):
        self.host_guard()
        if self.pid != os.getpid() or self.directory is None or self.lock is None:
            raise PermissionError("The host journal controller identity changed")
        current = trusted_directory(self.path)
        try:
            info = os.fstat(current)
            if (set(os.listdir(current)) - {"controller.lock", "state.json"}
                    or (info.st_dev, info.st_ino) != self.directory_identity or stat.S_IMODE(info.st_mode) != 0o700):
                raise JournalRetained("The host journal directory identity changed")
        finally:
            os.close(current)
        info = os.stat("controller.lock", dir_fd=self.directory, follow_symlinks=False)
        self.private_file(info)
        if (info.st_dev, info.st_ino) != self.lock_identity:
            raise JournalRetained("The host journal lock identity changed")
        raw, identity = self.read()
        if raw != self.raw or identity != self.file_identity:
            raise JournalRetained("The host journal state was replaced or changed")

    def read(self):
        try:
            fd = os.open("state.json", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC,
                         dir_fd=self.directory)
        except FileNotFoundError:
            return None, None
        try:
            info = os.fstat(fd)
            self.private_file(info)
            data = bytearray()
            while True:
                block = os.read(fd, min(8192, LIMIT + 1 - len(data)))
                if not block:
                    break
                data.extend(block)
                if len(data) > LIMIT:
                    raise ValueError("Host journal exceeds its limit")
            return bytes(data), (info.st_dev, info.st_ino)
        finally:
            os.close(fd)

    def snapshot(self):
        self.guard()
        return json.loads(json.dumps(self.data))

    def save(self, data):
        self.guard()
        self.validate(data)
        raw = (json.dumps(data, ensure_ascii=True, sort_keys=True, separators=(",", ":")) + "\n").encode()
        if len(raw) > LIMIT:
            raise ValueError("Host journal exceeds its limit")
        atomic_write(self.directory, "state.json", raw, mode=0o600)
        observed, identity = self.read()
        if observed != raw:
            raise JournalRetained("Host journal publication was not captured")
        self.data, self.raw, self.file_identity = json.loads(raw), raw, identity

    def begin(self, account, source):
        data = self.snapshot()
        if any(record["token"] not in self.known or record["pending"] is not None
                or ("dns" in record and record["dns"]["pending"] is not None)
                or ("session" in record and record["session"]["pending"] is not None)
                or ("forwarding" in record and record["forwarding"]["pending"] is not None) for record in data["profiles"]):
            # Complete current leases may coexist; unresolved mutation intent
            # blocks new allocation even when this controller created it.
            raise JournalRetained("Existing profile resources require reconciliation")
        token = uuid.uuid4().hex
        data["profiles"].append({"token": token, "account": asdict(account),
            "source": {"id": source.id, "name": source.name, "uid": source.uid, "vt": source.vt},
            "pending": "create", "network": None})
        self.save(data)
        self.known.add(token)
        return token

    def record(self, token):
        matches = [value for value in self.snapshot()["profiles"] if value["token"] == token]
        if len(matches) != 1:
            raise JournalRetained("The captured profile journal entry disappeared")
        return matches[0]

    def change(self, expected, replacement):
        data = self.snapshot()
        matches = [value for value in data["profiles"] if value["token"] == expected["token"]]
        if matches != [expected]:
            raise JournalRetained("The captured profile journal entry changed")
        data["profiles"].remove(expected)
        if replacement is not None:
            data["profiles"].append(replacement)
        if replacement is not None and "dns" in replacement:
            data["schema"] = max(data["schema"],2)
        if replacement is not None and "session" in replacement:
            data["schema"] = max(data["schema"],3)
        if replacement is not None and "forwarding" in replacement:
            data["schema"] = 4
        self.save(data)
        if replacement is None:
            self.known.discard(expected["token"])

    def close(self):
        # Retain all on-disk receipts; closing descriptors is never recovery.
        if self.lock is not None:
            os.close(self.lock); self.lock = None
        if self.directory is not None:
            os.close(self.directory); self.directory = None

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()
