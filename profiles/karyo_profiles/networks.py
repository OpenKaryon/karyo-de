# SPDX-License-Identifier: GPL-3.0-or-later
"""Own a fresh profile namespace and point-to-point link; never adopt old objects.

This is the kernel resource layer, not a desktop starter or an Internet gateway.
The future supervisor must keep the lease alive and stop its consumers before
closing it. Uncertain resources are retained, not removed by name on a retry.
"""
from dataclasses import dataclass
import fcntl
import ipaddress
import json
import os
from pathlib import Path
import re
import selectors
import subprocess
import time
import uuid

from .storage import trusted_directory


class NetworkError(RuntimeError):
    pass


class NetworkRetained(NetworkError):
    def __init__(self, lease):
        super().__init__("Profile network ownership is uncertain; resources were retained: " + lease.name)
        self.lease = lease


class NetworkCommand:
    def run(self, *arguments, input_data=None):
        if input_data is not None and (not isinstance(input_data, bytes) or len(input_data) > 4096):
            raise ValueError("Native network input must be at most one bounded pipe buffer")
        # No shell, caller environment, inherited descriptors or unbounded output.
        with subprocess.Popen(arguments, stdin=subprocess.PIPE if input_data is not None else subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL, close_fds=True,
                env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"}) as process:
            deadline = time.monotonic() + 10
            data = bytearray()
            try:
                if input_data is not None:
                    # At most Linux PIPE_BUF bytes: the initial write cannot
                    # block behind a child that has not started reading yet.
                    process.stdin.write(input_data)
                    process.stdin.close()
                with selectors.DefaultSelector() as ready:
                    ready.register(process.stdout, selectors.EVENT_READ)
                    while True:
                        remaining = deadline - time.monotonic()
                        if remaining <= 0 or not ready.select(remaining):
                            raise TimeoutError("Profile network command timed out")
                        block = os.read(process.stdout.fileno(), 8192)
                        if not block:
                            break
                        data.extend(block)
                        if len(data) > 256 * 1024:
                            raise NetworkError("Profile network response exceeds its limit")
                process.wait(timeout=max(0.001, deadline - time.monotonic()))
                if process.returncode:
                    raise NetworkError("Profile network command failed")
                return data.decode("utf-8")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


@dataclass(frozen=True)
class NetworkSpec:
    uid: int
    token: str

    def __post_init__(self):
        if type(self.uid) is not int or not 1000 <= self.uid < 17384:
            raise ValueError("The desktop UID is outside the private network pool")
        if not isinstance(self.token, str) or re.fullmatch(r"[a-f0-9]{32}", self.token) is None:
            raise ValueError("Invalid private network ownership token")

    @property
    def name(self):
        return "karyo-" + self.token

    @property
    def interface(self):
        return "kp" + self.token[:12]

    @property
    def alias(self):
        return "karyo:" + self.token

    @property
    def subnet(self):
        return ipaddress.IPv4Network((int(ipaddress.IPv4Address("10.231.0.0")) + 4 * (self.uid - 1000), 30))

    @property
    def host(self):
        return str(self.subnet.network_address + 1)

    @property
    def guest(self):
        return str(self.subnet.network_address + 2)


def _rows(text):
    rows = json.loads(text)
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise NetworkError("Invalid native network inventory")
    return rows


def _links(text):
    result = {}
    for row in _rows(text):
        name, index = row.get("ifname"), row.get("ifindex")
        if (not isinstance(name, str) or not name or name in result
                or type(index) is not int or index <= 0):
            raise NetworkError("Ambiguous native link inventory")
        result[name] = row
    return result


class ProfileNetwork:
    def __init__(self, store, spec, command=None):
        self.store, self.spec = store, spec
        self.command = NetworkCommand() if command is None else command
        self.fd = None
        self.identity = None
        self.indices = None
        self.tagged = set()
        self.namespace_created = False
        self.attempted = False
        self.pair_attempted = False
        self.closed = False

    @property
    def name(self):
        return self.spec.name

    @property
    def path(self):
        return "/run/netns/" + self.name

    def _guard(self):
        if os.getuid() != 0 or os.geteuid() != 0 or not self.store._locked:
            raise PermissionError("Profile networks require root and the held configuration lock")
        if self.closed:
            raise NetworkError("The profile network lease is closed")

    def _ip(self, *arguments, inside=False):
        prefix = ("/usr/sbin/ip", "-n", self.name) if inside else ("/usr/sbin/ip",)
        return self.command.run(*prefix, *arguments)

    def _inventory(self, inside=False):
        return _links(self._ip("-j", "-d", "link", "show", inside=inside))

    def _pin(self):
        directory = trusted_directory("/run/netns")
        try:
            fd = os.open(self.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=directory)
        finally:
            os.close(directory)
        try:
            # Linux NS_GET_NSTYPE, CLONE_NEWNET. A plain file is never a lease.
            if fcntl.ioctl(fd, 0xb703) != 0x40000000:
                raise NetworkError("The profile namespace path has the wrong native type")
            info = os.fstat(fd)
            return fd, (info.st_dev, info.st_ino)
        except BaseException:
            os.close(fd)
            raise

    def _verify_namespace(self):
        if self.fd is None:
            raise NetworkError("The created namespace identity could not be captured")
        fd, identity = self._pin()
        os.close(fd)
        if identity != self.identity:
            raise NetworkError("The profile namespace path was replaced")

    def _verify_pair(self):
        host = self._inventory().get(self.spec.interface)
        links = self._inventory(inside=True)
        peer = links.get("eth0")
        if set(links) != {"lo", "eth0"} or host is None or peer is None:
            raise NetworkError("The profile link inventory changed")
        for side, row in (("host", host), ("peer", peer)):
            alias = self.spec.alias if side in self.tagged else None
            if (row.get("ifalias") != alias or row.get("linkinfo", {}).get("info_kind") != "veth"
                    or "master" in row):
                raise NetworkError("The profile link ownership changed")
        indices = (host["ifindex"], peer["ifindex"])
        if (host.get("link_index") != indices[1] or peer.get("link_index") != indices[0]
                or (self.indices is not None and indices != self.indices)):
            raise NetworkError("The profile link pair identity changed")
        return indices

    def verify(self):
        self._guard()
        self._verify_namespace()
        if self.indices is None or self.tagged != {"host", "peer"}:
            raise NetworkError("The profile link identity is incomplete")
        self._verify_pair()

    def _empty(self):
        pids = self._ip("netns", "pids", self.name).splitlines()
        if pids:
            raise NetworkError("The profile namespace still has processes; none were terminated")

    def create(self):
        self._guard()
        if self.attempted:
            raise NetworkError("The profile network lease was already used")
        # Refuse collisions, including dangling symlinks and overlapping VPN routes.
        directory = trusted_directory("/run/netns", create=True)
        try:
            try:
                os.stat(self.name, dir_fd=directory, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise NetworkError("The requested namespace name already exists")
        finally:
            os.close(directory)
        if self.spec.interface in self._inventory():
            raise NetworkError("The requested host link already exists")
        for route in _rows(self._ip("-4", "-j", "route", "show", "table", "all")):
            destination = route.get("dst")
            if not isinstance(destination, str):
                raise NetworkError("The host route inventory is incomplete")
            if destination == "default":
                continue
            network = ipaddress.IPv4Network(destination, strict=False)
            if network.prefixlen and network.overlaps(self.spec.subnet):
                raise NetworkError("The private desktop subnet overlaps an existing host route")
        self.attempted = True
        try:
            self._ip("netns", "add", self.name)
        except BaseException as error:
            # A timed-out native mutation may already have taken effect. We do
            # not acquire or delete an object after an unacknowledged creation.
            raise NetworkRetained(self) from error
        self.namespace_created = True
        try:
            self.fd, self.identity = self._pin()
            self.pair_attempted = True
            # RTM_NEWLINK veth creation does not reliably apply IFLA_IFALIAS.
            # Capture the acknowledged fresh pair first, then tag each end via
            # a separate native mutation. Track successful tags independently
            # so rollback never assumes an unacknowledged alias was applied.
            self._ip("link", "add", "name", self.spec.interface,
                     "type", "veth", "peer", "name", "eth0", "netns", self.name)
            self.indices = self._verify_pair()
            self._ip("link", "set", "dev", self.spec.interface, "alias", self.spec.alias)
            self.tagged.add("host")
            self._ip("link", "set", "dev", "eth0", "alias", self.spec.alias, inside=True)
            self.tagged.add("peer")
            self.command.run("/usr/sbin/sysctl", "-qw",
                             "net.ipv6.conf." + self.spec.interface + ".disable_ipv6=1")
            self._verify_namespace()
            self.command.run("/usr/bin/nsenter", "--net=" + self.path, "--", "/usr/sbin/sysctl", "-qw",
                             "net.ipv6.conf.all.disable_ipv6=1", "net.ipv6.conf.default.disable_ipv6=1")
            self._ip("address", "add", self.spec.host + "/30", "dev", self.spec.interface)
            self._ip("address", "add", self.spec.guest + "/30", "dev", "eth0", inside=True)
            for interface in ("lo", "eth0"):
                self._ip("link", "set", "dev", interface, "up", inside=True)
            self._ip("link", "set", "dev", self.spec.interface, "up")
            self._ip("route", "add", "default", "via", self.spec.host, inside=True)
            self.verify()
            return self
        except BaseException as error:
            try:
                self.close()
            except BaseException:
                raise NetworkRetained(self) from error
            raise

    def close(self):
        self._guard()
        if not self.namespace_created:
            raise NetworkError("The namespace creation was not acknowledged")
        try:
            self._verify_namespace()
            self._empty()
            if self.pair_attempted:
                if self.indices is None:
                    raise NetworkError("The link creation identity is uncertain")
                self._verify_pair()
                self._ip("link", "delete", "dev", self.spec.interface)
                self.pair_attempted = False
                self.indices = None
            if set(self._inventory(inside=True)) != {"lo"}:
                raise NetworkError("The namespace has an unknown network device")
            self._verify_namespace()
            self._empty()
            self._ip("netns", "delete", self.name)
        except BaseException as error:
            raise NetworkRetained(self) from error
        os.close(self.fd)
        self.fd = None
        self.closed = True


def create_network(store, uid, command=None):
    """Called only after authoritative account and stopped-session eligibility checks."""
    return ProfileNetwork(store, NetworkSpec(uid, uuid.uuid4().hex), command).create()
