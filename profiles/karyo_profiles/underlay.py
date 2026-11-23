# SPDX-License-Identifier: GPL-3.0-or-later
"""Discover one live physical IPv4 gateway without following the Home VPN."""
from dataclasses import dataclass
import ipaddress
from pathlib import Path
import re

from .networks import NetworkCommand, NetworkError, _links, _rows

INTERFACE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.:-]{0,14}")


def interface_name(value):
    if not isinstance(value, str) or INTERFACE.fullmatch(value) is None:
        raise NetworkError("Invalid native interface name")
    return value


def _address(value):
    address = ipaddress.IPv4Address(value)
    if address.is_unspecified or address.is_multicast or address.is_loopback or int(address) == 0xffffffff:
        raise NetworkError("The physical network has an unusable IPv4 address")
    return address


def physical_device(name):
    # Virtual tun/wg/veth/bridge devices have no physical device link. Follow
    # only this kernel-owned sysfs link, not an application-provided path.
    interface_name(name)
    try:
        device = (Path("/sys/class/net") / name / "device").resolve(strict=True)
    except FileNotFoundError:
        return False
    return device.is_relative_to("/sys/devices")


@dataclass(frozen=True)
class PhysicalUnderlay:
    interface: str
    index: int
    address: str
    prefix: int
    gateway: str
    metric: int

    def __post_init__(self):
        interface_name(self.interface)
        if type(self.index) is not int or self.index <= 0:
            raise NetworkError("Invalid physical interface identity")
        if type(self.prefix) is not int or not 1 <= self.prefix <= 30:
            raise NetworkError("The physical gateway requires an ordinary connected IPv4 subnet")
        if type(self.metric) is not int or not 0 <= self.metric <= 0xffffffff:
            raise NetworkError("Invalid physical default route metric")
        address, gateway = _address(self.address), _address(self.gateway)
        if str(address) != self.address or str(gateway) != self.gateway:
            raise NetworkError("The physical address must use canonical IPv4 notation")
        network = ipaddress.IPv4Network((address, self.prefix), strict=False)
        if (gateway not in network or address == gateway
                or address in {network.network_address, network.broadcast_address}
                or gateway in {network.network_address, network.broadcast_address}):
            raise NetworkError("The physical gateway is not in its connected subnet")

    @property
    def routing_identity(self):
        # Metric selects the best physical path; it does not appear in the
        # already pinned private routes or interface-specific firewall.
        return (self.interface, self.index, self.address, self.prefix, self.gateway)

    @property
    def subnet(self):
        return str(ipaddress.IPv4Network((self.address, self.prefix), strict=False))


def discover_underlay(command=None, physical=physical_device):
    command = NetworkCommand() if command is None else command
    links = _links(command.run("/usr/sbin/ip", "-j", "-d", "link", "show"))
    routes = _rows(command.run("/usr/sbin/ip", "-4", "-j", "route", "show", "table", "main"))
    addresses = _rows(command.run("/usr/sbin/ip", "-4", "-j", "address", "show"))
    by_index = {}
    for row in addresses:
        index = row.get("ifindex")
        if type(index) is not int or index <= 0 or index in by_index:
            raise NetworkError("Ambiguous physical address inventory")
        by_index[index] = row
    candidates = set()
    for route in routes:
        if route.get("dst") not in {"default", "0.0.0.0/0"} or route.get("type", "unicast") != "unicast":
            continue
        name = interface_name(route.get("dev"))
        link = links.get(name)
        if link is None:
            raise NetworkError("A default route refers to an unknown native interface")
        if not physical(name):
            continue
        if (not {"UP", "LOWER_UP"}.issubset(link.get("flags", [])) or "master" in link
                or link.get("linkinfo", {}).get("info_kind") in {"tun", "wireguard", "veth", "bridge"}):
            continue
        if route.get("flags") or "multipath" in route or "nexthops" in route:
            raise NetworkError("The physical default route requires explicit multipath support")
        index = link["ifindex"]
        row = by_index.get(index)
        if row is None or row.get("ifname") != name:
            raise NetworkError("The physical address no longer matches its native link")
        valid = []
        for entry in row.get("addr_info", []):
            if entry.get("family") != "inet" or entry.get("scope") != "global":
                continue
            if (entry.get("tentative") or entry.get("dadfailed") or entry.get("deprecated")
                    or entry.get("valid_life_time") in (0, "0")):
                continue
            address = entry.get("local")
            if route.get("prefsrc") is not None and route["prefsrc"] != address:
                continue
            try:
                valid.append(PhysicalUnderlay(name, index, address, entry.get("prefixlen"),
                                               route.get("gateway"), route.get("metric", 0)))
            except (ValueError, TypeError):
                continue
        if len(set(valid)) != 1:
            raise NetworkError("The physical default route has no unambiguous connected address")
        candidates.update(valid)
    if not candidates:
        raise NetworkError("No supported live physical IPv4 gateway is available")
    metric = min(item.metric for item in candidates)
    best = [item for item in candidates if item.metric == metric]
    if len(best) != 1:
        raise NetworkError("Multiple physical gateways have equal priority")
    return best[0]
