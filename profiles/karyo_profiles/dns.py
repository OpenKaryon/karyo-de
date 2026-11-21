# SPDX-License-Identifier: GPL-3.0-or-later
"""Physical-link DNS discovery and private resolved service configuration.

The supervisor must start the returned fixed services, validate their ownership,
seed the private bus and wait for readiness before starting any desktop clients.
This module neither starts a unit nor changes the host resolver.
"""
from dataclasses import dataclass
import ipaddress
import json
import os
from pathlib import Path
import re
import stat

from .credentials import LocalAccounts, database_lines
from .networks import NetworkCommand, NetworkError, NetworkSpec
from .storage import atomic_write, trusted_directory
from .underlay import PhysicalUnderlay, discover_underlay

LINK_PATH = re.compile(r"/org/freedesktop/resolve1/link/[A-Za-z0-9_]{1,48}")
SAFE_PATH = re.compile(r"/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+")


def _reply(text, signature):
    value = json.loads(text)
    if not isinstance(value, dict) or set(value) != {"type", "data"} or value["type"] != signature:
        raise NetworkError("Invalid native DNS property signature")
    return value["data"]


def _domain(value):
    if not isinstance(value, str) or not value.isascii() or not 1 <= len(value) <= 253:
        raise NetworkError("Invalid physical DNS search domain")
    value = value.removesuffix(".").lower()
    if any(re.fullmatch(r"[a-z0-9_](?:[a-z0-9_-]{0,61}[a-z0-9_])?", label) is None
           for label in value.split(".")):
        raise NetworkError("Invalid physical DNS search domain")
    return value


@dataclass(frozen=True)
class DNSSeed:
    physical: PhysicalUnderlay
    servers: tuple
    search: tuple = ()

    def __post_init__(self):
        if not isinstance(self.physical, PhysicalUnderlay) or type(self.servers) is not tuple or not 1 <= len(self.servers) <= 16:
            raise NetworkError("Physical DNS needs a bounded immutable IPv4 server list")
        for value in self.servers:
            address = ipaddress.IPv4Address(value)
            if (str(address) != value or address.is_unspecified or address.is_loopback or address.is_multicast
                    or int(address) == 0xffffffff or address in ipaddress.IPv4Network("10.231.0.0/16")):
                raise NetworkError("Physical DNS must not use a host stub or a private desktop address")
        if len(set(self.servers)) != len(self.servers):
            raise NetworkError("Duplicate physical DNS servers")
        if type(self.search) is not tuple or len(self.search) > 32 or any(_domain(v) != v for v in self.search):
            raise NetworkError("Invalid immutable physical DNS search list")
        if len(set(self.search)) != len(self.search):
            raise NetworkError("Duplicate physical DNS search domains")


def discover_dns(physical, command=None):
    command = NetworkCommand() if command is None else command
    if discover_underlay(command) != physical:
        raise NetworkError("The physical network changed before DNS discovery")
    prefix = ("/usr/bin/busctl", "--system", "--json=short", "--timeout=5s")
    data = _reply(command.run(*prefix, "call", "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
                              "org.freedesktop.resolve1.Manager", "GetLink", "i", str(physical.index)), "o")
    if not isinstance(data, list) or len(data) != 1 or not isinstance(data[0], str) or LINK_PATH.fullmatch(data[0]) is None:
        raise NetworkError("Invalid physical resolver link identity")
    def property(name, signature):
        return _reply(command.run(*prefix, "get-property", "org.freedesktop.resolve1", data[0],
                                  "org.freedesktop.resolve1.Link", name), signature)
    transport = property("DNSOverTLS", "s")
    if not isinstance(transport, str):
        raise NetworkError("Invalid physical DNS transport property")
    if transport == "":
        transport = _reply(command.run(*prefix, "get-property", "org.freedesktop.resolve1",
                                        "/org/freedesktop/resolve1", "org.freedesktop.resolve1.Manager", "DNSOverTLS"), "s")
    if transport != "no":
        raise NetworkError("Physical encrypted DNS transport needs explicit profile support")
    extended = property("DNSEx", "a(iayqs)")
    if not isinstance(extended, list) or len(extended) > 32:
        raise NetworkError("Invalid extended physical DNS inventory")
    extended_v4 = []
    for endpoint in extended:
        if (not isinstance(endpoint, list) or len(endpoint) != 4 or type(endpoint[0]) is not int
                or endpoint[0] not in {2, 10} or not isinstance(endpoint[1], list)
                or len(endpoint[1]) != (4 if endpoint[0] == 2 else 16)
                or any(type(v) is not int or not 0 <= v <= 255 for v in endpoint[1])
                or type(endpoint[2]) is not int or endpoint[2] not in {0, 53} or endpoint[3] != ""):
            raise NetworkError("Invalid or unsupported extended physical DNS endpoint")
        if endpoint[0] == 2:
            value = str(ipaddress.IPv4Address(bytes(endpoint[1])))
            if value not in extended_v4:
                extended_v4.append(value)
    rows = property("DNS", "a(iay)")
    if not isinstance(rows, list) or len(rows) > 32:
        raise NetworkError("Oversized native physical DNS inventory")
    servers = []
    for row in rows:
        if (not isinstance(row, list) or len(row) != 2 or type(row[0]) is not int or row[0] not in {2, 10}
                or not isinstance(row[1], list) or len(row[1]) != (4 if row[0] == 2 else 16)
                or any(type(value) is not int or not 0 <= value <= 255 for value in row[1])):
            raise NetworkError("Invalid native DNS address tuple")
        if row[0] == 2:
            address = str(ipaddress.IPv4Address(bytes(row[1])))
            if address not in servers:
                servers.append(address)
    if servers != extended_v4:
        raise NetworkError("Physical DNS changed between native property snapshots")
    domains = property("Domains", "a(sb)")
    if not isinstance(domains, list) or len(domains) > 64:
        raise NetworkError("Oversized native DNS domain inventory")
    search = []
    for row in domains:
        if not isinstance(row, list) or len(row) != 2 or type(row[1]) is not bool:
            raise NetworkError("Invalid native DNS domain tuple")
        if row[0] == "." and row[1]:
            continue
        domain = _domain(row[0])
        if not row[1] and domain not in search:
            search.append(domain)
    if discover_underlay(command) != physical:
        raise NetworkError("The physical network changed during DNS discovery")
    return DNSSeed(physical, tuple(servers), tuple(search))


def supported_resolver_layout(path=Path("/etc/resolv.conf")):
    """Refuse an unscoped libc file layout before preparing client mounts."""
    info = os.lstat(path)
    if not stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_nlink != 1:
        raise NetworkError("Profile DNS requires the root-owned systemd-resolved resolver symlink")
    target = os.readlink(path)
    for name in ("stub-resolv.conf", "resolv.conf"):
        if target in {"/run/systemd/resolve/" + name, "../run/systemd/resolve/" + name}:
            return name
    raise NetworkError("The host resolver symlink targets an unsupported DNS backend")


def resolver_service_uid():
    rows = list(database_lines("/etc/passwd"))
    candidates = [r for r in rows if r[0] == "systemd-resolve"]
    if (len(candidates) != 1 or len(candidates[0]) != 7 or not candidates[0][2].isascii()
            or not candidates[0][2].isdecimal() or not 0 < int(candidates[0][2]) < 1000):
        raise NetworkError("A unique native systemd-resolve service account is required")
    uid = int(candidates[0][2])
    if sum(len(r) == 7 and r[2].isdecimal() and int(r[2]) == uid for r in rows) != 1:
        raise NetworkError("The resolver service UID is ambiguous")
    return uid


@dataclass(frozen=True)
class ResolverPlan:
    spec: NetworkSpec
    seed: DNSSeed
    gid: int
    service_uid: int
    config: str

    def __post_init__(self):
        if not isinstance(self.spec, NetworkSpec) or not isinstance(self.seed, DNSSeed):
            raise NetworkError("Private DNS requires a validated network and DNS seed")
        if (type(self.gid) is not int or not 1000 <= self.gid < 65534 or type(self.service_uid) is not int
                or not 0 < self.service_uid < 1000):
            raise NetworkError("Invalid private DNS service or profile group identity")
        if not isinstance(self.config, str) or SAFE_PATH.fullmatch(self.config) is None or ".." in Path(self.config).parts:
            raise NetworkError("Invalid private resolver configuration path")

    @property
    def runtime_name(self):
        return "karyo-resolve-" + self.spec.token

    @property
    def bus_name(self):
        return "karyo-resolve-bus-" + self.spec.token

    @property
    def bus_address(self):
        return "unix:path=/run/" + self.bus_name + "/bus"

    @property
    def runtime_paths(self):
        return tuple("/run/" + v for v in (self.runtime_name, self.bus_name, self.runtime_name + "-netif"))

    def files(self):
        # Private configuration deliberately clears global/fallback upstreams.
        # Physical DNS is installed on private eth0 over the private D-Bus API.
        resolved = ("# Karyo private resolver; physical DNS is seeded per-link.\n[Resolve]\n"
                    "DNS=\nFallbackDNS=\nDomains=\nLLMNR=no\nMulticastDNS=no\nDNSStubListener=yes\n")
        bus = ('<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN" '
               '"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">\n'
               f'<busconfig><type>system</type><listen>{self.bus_address}</listen>'
               '<auth>EXTERNAL</auth><policy context="default"><deny user="*"/>'
               f'<allow user="root"/><allow user="{self.service_uid}"/><allow user="{self.spec.uid}"/>'
               '<deny own="*"/><deny send_destination="*"/><allow receive_sender="*"/></policy>'
               '<policy user="root"><allow send_destination="*"/></policy>'
               f'<policy user="{self.service_uid}">'
               '<allow own="org.freedesktop.resolve1"/><allow send_destination="*"/></policy>'
               f'<policy user="{self.spec.uid}">'
               '<allow send_destination="org.freedesktop.DBus"/>'
               '<allow send_destination="org.freedesktop.resolve1" send_interface="org.freedesktop.DBus.Properties"/>'
               '<allow send_destination="org.freedesktop.resolve1" send_interface="org.freedesktop.DBus.Introspectable"/>'
               '<allow send_destination="org.freedesktop.resolve1" send_interface="org.freedesktop.resolve1.Manager" send_member="GetLink"/>'
               '<allow send_destination="org.freedesktop.resolve1" send_interface="org.freedesktop.resolve1.Manager" send_member="ResolveHostname"/>'
               '<allow send_destination="org.freedesktop.resolve1" send_interface="org.freedesktop.resolve1.Manager" send_member="ResolveAddress"/>'
               '</policy></busconfig>\n')
        return {"resolved.conf": resolved.encode(), "bus.conf": bus.encode()}

    def bus_properties(self):
        return ("User=root", "Group=" + str(self.gid), "RuntimeDirectory=" + self.bus_name,
                "RuntimeDirectoryMode=0750", "NoNewPrivileges=yes", "PrivateTmp=yes")

    def resolver_properties(self):
        netif = self.runtime_name + "-netif"
        return ("User=systemd-resolve", "Group=" + str(self.gid),
                "NetworkNamespacePath=/run/netns/" + self.spec.name,
                "RuntimeDirectory=" + " ".join((self.runtime_name, netif)), "RuntimeDirectoryMode=0750",
                "Environment=DBUS_SYSTEM_BUS_ADDRESS=" + self.bus_address,
                "AmbientCapabilities=CAP_SETPCAP CAP_NET_RAW CAP_NET_BIND_SERVICE",
                "CapabilityBoundingSet=CAP_SETPCAP CAP_NET_RAW CAP_NET_BIND_SERVICE",
                "NoNewPrivileges=yes", "PrivateTmp=yes", "PrivateDevices=yes", "ProtectSystem=strict",
                "BindPaths=/run/" + self.runtime_name + ":/run/systemd/resolve",
                "BindReadOnlyPaths=" + self.config + "/resolved.conf:/etc/systemd/resolved.conf /run/" + netif + ":/run/systemd/netif",
                "InaccessiblePaths=-/etc/systemd/resolved.conf.d -/run/systemd/resolved.conf.d -/usr/lib/systemd/resolved.conf.d -/usr/local/lib/systemd/resolved.conf.d")

    def client_properties(self):
        # These are only DNS/network scope properties. In particular do not
        # restrict the user's capability bounding set or normal password sudo.
        return ("NetworkNamespacePath=/run/netns/" + self.spec.name,
                "BindReadOnlyPaths=/run/" + self.runtime_name + ":/run/systemd/resolve",
                "InaccessiblePaths=-/run/nscd/socket -/run/avahi-daemon/socket")


class ResolverFiles:
    def __init__(self, network, seed, accounts=None):
        self.network = network
        self.accounts = LocalAccounts() if accounts is None else accounts
        account = self.accounts.by_uid(network.spec.uid)
        self.plan = ResolverPlan(network.spec, seed, account.gid, resolver_service_uid(),
                                 str(network.store.runtime / "resolver-config" / network.spec.token))
        self.fd = None
        self.identity = None
        self.captured = {}
        self.closed = False

    def _guard(self):
        self.network.verify()
        if self.closed:
            raise NetworkError("The private resolver configuration lease is closed")

    def create(self):
        self._guard()
        self.network._empty()
        supported_resolver_layout()
        if self.fd is not None:
            raise NetworkError("The private resolver configuration already exists")
        parent = Path(self.plan.config).parent
        directory = trusted_directory(parent, create=True)
        try:
            os.mkdir(self.network.spec.token, 0o755, dir_fd=directory)
            self.fd = os.open(self.network.spec.token, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                              dir_fd=directory)
            os.fchmod(self.fd, 0o755)
            info = os.fstat(self.fd)
            self.identity = info.st_dev, info.st_ino
        finally:
            os.close(directory)
        # A write failure retains this known directory for explicit cleanup;
        # no deletion of a preexisting directory or recursive recovery occurs.
        for name, data in self.plan.files().items():
            atomic_write(self.fd, name, data)
            info = os.stat(name, dir_fd=self.fd, follow_symlinks=False)
            self.captured[name] = (info.st_dev, info.st_ino, data)
        return self

    def close(self):
        self._guard()
        self.network._empty()
        if self.fd is None:
            raise NetworkError("The private resolver directory was not captured")
        if any(os.path.lexists(path) for path in self.plan.runtime_paths):
            raise NetworkError("Private DNS services or retained runtime directories still exist")
        current = trusted_directory(self.plan.config)
        try:
            info = os.fstat(current)
            if (info.st_dev, info.st_ino) != self.identity or set(os.listdir(current)) != set(self.captured):
                raise NetworkError("Private resolver configuration ownership changed")
            for name, (device, inode, data) in self.captured.items():
                fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=current)
                try:
                    info = os.fstat(fd)
                    if (not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_nlink != 1
                            or info.st_mode & 0o022 or (info.st_dev, info.st_ino) != (device, inode)
                            or os.read(fd, len(data) + 1) != data):
                        raise NetworkError("A private resolver configuration file changed")
                finally:
                    os.close(fd)
            for name in self.captured:
                os.unlink(name, dir_fd=current)
        finally:
            os.close(current)
        parent = trusted_directory(Path(self.plan.config).parent)
        try:
            info = os.stat(self.network.spec.token, dir_fd=parent, follow_symlinks=False)
            if (info.st_dev, info.st_ino) != self.identity:
                raise NetworkError("The private resolver directory changed before removal")
            os.rmdir(self.network.spec.token, dir_fd=parent)
        finally:
            os.close(parent)
        os.close(self.fd)
        self.fd = None
        self.closed = True


def seed_private_dns(files):
    """After proving the owned resolver unit, configure its private eth0 link.

    Do not call this against the host bus with a namespace-local interface ID.
    The private bus has root-owned parent permissions and an explicit name owner.
    """
    files._guard()
    plan, network = files.plan, files.network
    link = network._inventory(inside=True).get("eth0")
    if link is None or type(link.get("ifindex")) is not int:
        raise NetworkError("The private resolver link is not ready")
    prefix = ("/usr/bin/busctl", "--address=" + plan.bus_address, "--json=short", "--timeout=5s")
    data = _reply(network.command.run(*prefix, "call", "org.freedesktop.resolve1", "/org/freedesktop/resolve1",
                                      "org.freedesktop.resolve1.Manager", "GetLink", "i", str(link["ifindex"])), "o")
    if not isinstance(data, list) or len(data) != 1 or not isinstance(data[0], str) or LINK_PATH.fullmatch(data[0]) is None:
        raise NetworkError("Invalid private resolver link object")
    def call(method, signature, *values):
        return network.command.run(*prefix, "call", "org.freedesktop.resolve1", data[0],
                                   "org.freedesktop.resolve1.Link", method, signature, *values)
    addresses = []
    for server in plan.seed.servers:
        addresses.extend(("2", "4", *map(str, ipaddress.IPv4Address(server).packed)))
    call("SetDNS", "a(iay)", str(len(plan.seed.servers)), *addresses)
    domains = []
    for domain in plan.seed.search:
        domains.extend((domain, "false"))
    call("SetDomains", "a(sb)", str(len(plan.seed.search)), *domains)
    call("SetDefaultRoute", "b", "true")
