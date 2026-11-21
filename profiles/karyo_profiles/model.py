# SPDX-License-Identifier: GPL-3.0-or-later
"""Validated ownership, protection and address policy for optional desktop helpers."""
from dataclasses import dataclass
import ipaddress
import os
from pathlib import Path
import re
import stat
import unicodedata

CONFIG = Path("/etc/karyo/desktops.conf")
LEGACY_CONFIG = Path("/etc/openkaryon/desktops.conf")
MAX_CONFIG_BYTES = 64 * 1024
NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_.-]*\$?")


def account_name(value):
    if not isinstance(value, str) or len(value) > 32 or NAME.fullmatch(value) is None:
        raise ValueError("Invalid local desktop account name")
    return value


def label(value):
    if not isinstance(value, str):
        raise ValueError("Desktop label must be text")
    if any(unicodedata.category(c).startswith("C") or unicodedata.category(c) in {"Zl", "Zp"} for c in value):
        raise ValueError("Desktop label must not contain control characters")
    value = value.strip()
    if not value or len(value) > 80 or any(c in value for c in ":,"):
        raise ValueError("Desktop label must be a single safe GECOS name of at most 80 characters")
    return value


def bounded_text(value):
    if not isinstance(value, str) or len(value.encode("utf-8")) > MAX_CONFIG_BYTES:
        raise ValueError("Desktop configuration is too large")
    if "\0" in value:
        raise ValueError("Desktop configuration contains NUL")
    return value


def root_file(path, *, dir_fd=None):
    """Read one bounded descriptor; never follow a final symlink or wait on a FIFO."""
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=dir_fd)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError("Desktop configuration must be a root-owned non-writable regular file")
        if info.st_size > MAX_CONFIG_BYTES:
            raise ValueError("Desktop configuration is too large")
        content = bytearray()
        while len(content) <= MAX_CONFIG_BYTES:
            block = os.read(fd, min(8192, MAX_CONFIG_BYTES + 1 - len(content)))
            if not block:
                break
            content.extend(block)
        if len(content) > MAX_CONFIG_BYTES:
            raise ValueError("Desktop configuration grew beyond its limit")
        return content.decode("utf-8")
    finally:
        os.close(fd)


@dataclass(frozen=True)
class Ownership:
    families: tuple = ()

    def __post_init__(self):
        if type(self.families) is not tuple:
            raise ValueError("Desktop families must be immutable tuples")
        seen = set()
        for family in self.families:
            if type(family) is not tuple or not family:
                raise ValueError("A desktop family needs a main account")
            for name in family:
                account_name(name)
                if name in seen:
                    raise ValueError("A desktop account must occur exactly once")
                seen.add(name)

    @classmethod
    def parse(cls, text):
        rows = []
        for line in bounded_text(text).splitlines():
            names = tuple(line.partition("#")[0].split())
            if names:
                rows.append(names)
        return cls(tuple(rows))

    @classmethod
    def load(cls, current=CONFIG, legacy=LEGACY_CONFIG):
        try:
            return cls.parse(root_file(current))
        except FileNotFoundError:
            try:
                return cls.parse(root_file(legacy))
            except FileNotFoundError:
                return cls()
        # An invalid, unsafe or unreadable current file must never fall back.

    def family(self, name):
        account_name(name)
        return next((row for row in self.families if name in row), (name,))

    def owner(self, name):
        return self.family(name)[0]

    def is_child(self, name):
        return self.owner(name) != name

    def with_child(self, caller, child):
        account_name(caller)
        account_name(child)
        if child == caller or any(child in row for row in self.families):
            raise ValueError("A new desktop cannot already own or belong to a family")
        family = self.family(caller)
        if family in self.families:
            rows = tuple((*row, child) if row == family else row for row in self.families)
        else:
            rows = (*self.families, (caller, child))
        return Ownership(rows)

    def serialize(self):
        return "".join(" ".join(row) + "\n" for row in self.families)


@dataclass(frozen=True)
class DesktopSettings:
    password: str = "none"
    vpn: str = "none"
    workstation: bool = False

    def __post_init__(self):
        if (not isinstance(self.password, str) or self.password not in {"none", "home", "separate"}
                or not isinstance(self.vpn, str) or self.vpn not in {"none", "netbird"}
                or type(self.workstation) is not bool):
            raise ValueError("Invalid desktop protection, VPN or workstation policy")

    @classmethod
    def parse(cls, text):
        values = {}
        allowed = {"PASSWORD": {"none", "home", "separate"}, "VPN": {"none", "netbird"},
                   "WORKSTATION": {"yes", "no"}}
        for line in bounded_text(text).splitlines():
            line = line.partition("#")[0].strip()
            if not line:
                continue
            key, separator, value = line.partition("=")
            key, value = key.strip(), value.strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            if not separator or key in values or key not in allowed or value not in allowed[key]:
                raise ValueError("Invalid, duplicate or unsupported desktop setting")
            values[key] = value
        return cls(values.get("PASSWORD", "none"), values.get("VPN", "none"),
                   values.get("WORKSTATION", "no") == "yes")

    @property
    def protected(self):
        return self.password != "none"

    def serialize(self):
        # Parse validates dataclass instances before they become privileged data.
        text = (f"PASSWORD={self.password}\nVPN={self.vpn}\n"
                f"WORKSTATION={'yes' if self.workstation else 'no'}\n")
        if type(self.workstation) is not bool:
            raise ValueError("Workstation marker must be boolean")
        self.parse(text)
        return text


@dataclass(frozen=True)
class DesktopAddress:
    subnet: ipaddress.IPv4Network
    host: ipaddress.IPv4Address
    child: ipaddress.IPv4Address
    interface: str

    @classmethod
    def for_uid(cls, uid, pool=ipaddress.IPv4Network("10.231.0.0/16")):
        if type(uid) is not int or not 1000 <= uid < 65534:
            raise ValueError("A desktop needs an ordinary local user ID")
        private = [ipaddress.IPv4Network(net) for net in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")]
        if (not isinstance(pool, ipaddress.IPv4Network) or pool.prefixlen > 30
                or not any(pool.subnet_of(net) for net in private)):
            raise ValueError("Desktop address pool must be a private IPv4 network of /30 or larger")
        offset = (uid - 1000) * 4
        if offset + 4 > pool.num_addresses:
            raise ValueError("User ID is outside the desktop address pool")
        network = ipaddress.IPv4Network((int(pool.network_address) + offset, 30))
        return cls(network, network.network_address + 1, network.network_address + 2, f"kd{uid}")
