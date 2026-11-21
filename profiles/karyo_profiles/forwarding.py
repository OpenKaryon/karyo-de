# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned IPv4 forwarding through the physical underlay, independent of Home VPN.

No host default route, foreign firewall chain or global sysctl is changed.
The optional runtime must explicitly provide host forwarding before using this
layer, and must stop every namespace consumer before teardown.
"""
import ipaddress
import json
from pathlib import Path

from .networks import NetworkError, _rows
from .underlay import discover_underlay


class ForwardingRetained(NetworkError):
    def __init__(self, gateway):
        super().__init__("Profile forwarding ownership is uncertain; resources retained: " + gateway.name)
        self.gateway = gateway


def _table(value):
    if type(value) is int and 0 < value <= 0xffffffff:
        return value
    if isinstance(value, str) and value.isascii() and value.isdecimal() and str(int(value)) == value:
        return _table(int(value))
    raise NetworkError("The native routing table is not a numeric identity")


def _canonical(rows):
    return sorted((json.dumps(row, sort_keys=True, separators=(",", ":")) for row in rows))


PRIVATE_POOL = ipaddress.IPv4Network("10.231.0.0/16")


def choose_priority(rows, interface, destination=None, *, source=None, slots=range(1, 65),
                    exclude_private_sources=False):
    used = set()
    blockers = []
    for row in rows:
        priority = row.get("priority")
        if type(priority) is not int or not 0 <= priority <= 0xffffffff:
            raise NetworkError("Invalid native routing rule priority")
        used.add(priority)
        if (priority == 0 and row.get("src") == "all" and _table(row.get("table")) == 255
                and set(row) <= {"priority", "src", "table", "protocol"}):
            continue
        # An earlier rule tied to a different incoming interface cannot divert
        # this profile. Inverted selectors cannot use that exception.
        if interface is not None and row.get("iif") and row["iif"] != interface and not row.get("not"):
            continue
        if destination is not None and row.get("dst", "all") != "all" and not row.get("not"):
            if not ipaddress.IPv4Network(row["dst"], strict=False).overlaps(destination):
                continue
        if row.get("src", "all") != "all" and not row.get("not"):
            prefix = ipaddress.IPv4Network(row["src"], strict=False)
            if source is not None and not prefix.overlaps(source):
                continue
            # Return traffic from another reserved desktop address is invalid
            # and explicitly dropped by the profile firewall. It cannot be a
            # physical reply or its reverse-path lookup.
            if exclude_private_sources and prefix.subnet_of(PRIVATE_POOL):
                continue
        blockers.append(priority)
    earliest = min(blockers, default=0xffffffff)
    priority = next((value for value in slots if value not in used and value < earliest), None)
    if priority is None:
        raise NetworkError("No early routing priority can pin this profile before existing VPN rules")
    return priority


class ProfileForwarding:
    def __init__(self, network, physical=None):
        self.network = network
        self.command = network.command
        self.physical = physical
        self.table = 40000 + network.spec.uid - 1000
        self.name = "karyo_fwd_" + network.spec.token
        self.priorities = []
        self.route_records = []
        self.route_snapshot = []
        self.rule_snapshots = []
        self.firewall_snapshot = None
        self.firewall_handle = None
        self.attempted = False
        self.uncertain = False
        self.closed = False

    def _guard(self):
        self.network.verify()
        if self.closed:
            raise NetworkError("The profile forwarding lease is closed")

    def _ip(self, *args):
        return self.command.run("/usr/sbin/ip", "-4", *args)

    def _routes(self):
        rows = []
        for row in _rows(self._ip("-j", "-N", "route", "show", "table", "all")):
            if _table(row.get("table", 254)) == self.table:
                row["table"] = str(self.table)
                if row.get("scope") in (253, "253"):
                    row["scope"] = "link"
                if row.get("type") in (7, "7"):
                    row["type"] = "unreachable"
                rows.append(row)
        return rows

    def _rules(self):
        return _rows(self._ip("-j", "-N", "rule", "show"))

    def _nft(self, *args, input_data=None):
        return self.command.run("/usr/sbin/nft", *args, input_data=input_data)

    def _firewall(self):
        value = json.loads(self._nft("-j", "list", "table", "inet", self.name))
        if not isinstance(value, dict) or not isinstance(value.get("nftables"), list):
            raise NetworkError("Invalid native profile firewall inventory")
        rows = [row for row in value["nftables"] if isinstance(row, dict) and "metainfo" not in row]
        tables = [row["table"] for row in rows if "table" in row]
        if (len(tables) != 1 or tables[0].get("name") != self.name or tables[0].get("family") != "inet"
                or tables[0].get("comment") != self.network.spec.alias
                or type(tables[0].get("handle")) is not int or tables[0]["handle"] <= 0):
            raise NetworkError("The profile firewall ownership changed")
        return rows, tables[0]["handle"]

    def _verify_owned(self):
        if self.uncertain:
            raise NetworkError("A native forwarding mutation was not acknowledged and captured")
        if _canonical(self._routes()) != _canonical(self.route_snapshot):
            raise NetworkError("The private routing table ownership changed")
        rules = [row for row in self._rules() if row.get("priority") in self.priorities
                 or ("table" in row and _table(row["table"]) == self.table)]
        if _canonical(rules) != _canonical(self.rule_snapshots):
            raise NetworkError("The profile routing rule ownership changed")
        if self.firewall_snapshot is not None:
            rows, handle = self._firewall()
            if rows != self.firewall_snapshot or handle != self.firewall_handle:
                raise NetworkError("The profile firewall contents or identity changed")

    def verify(self):
        self._guard()
        self._verify_owned()
        if len(self.rule_snapshots) != 2 or self.firewall_snapshot is None or len(self.route_records) != 4:
            raise NetworkError("The profile forwarding graph is incomplete")
        if discover_underlay(self.command) != self.physical:
            raise NetworkError("The physical network changed; profile forwarding requires reconciliation")

    def _add_route(self, args, fields):
        self.uncertain = True
        self._ip("route", "add", *args, "table", str(self.table), "proto", "static")
        current = self._routes()
        remaining = list(current)
        for old in self.route_snapshot:
            if old not in remaining:
                raise NetworkError("An existing private route changed during creation")
            remaining.remove(old)
        if (len(remaining) != 1 or any(remaining[0].get(key) != value for key, value in fields.items())
                or _table(remaining[0].get("table")) != self.table):
            raise NetworkError("The newly created private route could not be captured")
        self.route_records.append((args, remaining[0]))
        self.route_snapshot = current
        self.uncertain = False

    def _script(self):
        # All interpolations are canonical addresses or validated native names;
        # JSON quoting also makes nft string boundaries explicit. No shell.
        host, device = map(json.dumps, (self.network.spec.interface, self.physical.interface))
        guest = self.network.spec.guest
        return (f'create table inet {self.name} {{\n'
                f' comment {json.dumps(self.network.spec.alias)};\n'
                ' chain forward { type filter hook forward priority 0; policy accept;\n'
                f'  iifname {host} ip saddr != {guest} drop\n'
                f'  ip daddr {guest} oifname != {host} drop\n'
                f'  oifname {host} ip saddr {PRIVATE_POOL} drop\n'
                f'  iifname {host} oifname != {device} drop\n'
                f'  oifname {host} iifname != {device} drop\n'
                f'  oifname {host} ct state != {{ established, related }} drop\n'
                f'  iifname {host} oifname {device} ip saddr {guest} accept\n'
                f'  oifname {host} iifname {device} ct state {{ established, related }} accept\n'
                ' }\n'
                ' chain source_nat { type nat hook postrouting priority 100; policy accept;\n'
                f'  iifname {host} oifname {device} ip saddr {guest} masquerade\n'
                ' }\n}\n').encode("ascii")

    def create(self):
        self._guard()
        if self.attempted:
            raise NetworkError("The forwarding lease was already used")
        self.network._empty()
        if Path("/proc/sys/net/ipv4/ip_forward").read_text().strip() != "1":
            raise NetworkError("The optional profile runtime must explicitly enable host IPv4 forwarding")
        discovered = discover_underlay(self.command)
        if self.physical is not None and discovered != self.physical:
            raise NetworkError("The selected physical underlay changed before forwarding setup")
        self.physical = discovered
        if ipaddress.IPv4Network(self.physical.subnet).overlaps(PRIVATE_POOL):
            raise NetworkError("The physical network overlaps the reserved desktop address pool")
        if self._routes() or any(_table(row["table"]) == self.table for row in self._rules() if "table" in row):
            raise NetworkError("The requested private routing table is already in use")
        rules = self._rules()
        private = ipaddress.IPv4Network(self.network.spec.guest + "/32")
        # Source/destination selectors also cover the kernel's reverse-path
        # FIB lookups, whose incoming interface need not be the packet's veth.
        # Keep all source slots before return slots; the fixed VT pool needs
        # fewer than sixteen profiles. No foreign priority is moved or reused.
        outgoing = choose_priority(rules, None, source=private, slots=range(1, 17))
        planned = {"priority": outgoing, "src": self.network.spec.guest, "table": str(self.table)}
        returning = choose_priority(rules + [planned], None, private, slots=range(17, 33),
                                    exclude_private_sources=True)
        self.priorities = [outgoing, returning]
        tables = json.loads(self._nft("-j", "list", "tables"))
        if not isinstance(tables, dict) or not isinstance(tables.get("nftables"), list):
            raise NetworkError("Invalid native firewall table inventory")
        if any(row.get("table", {}).get("family") == "inet" and row["table"].get("name") == self.name
               for row in tables["nftables"]):
            raise NetworkError("The requested profile firewall table already exists")
        self.attempted = True
        try:
            self._add_route(("unreachable", "default", "metric", "32767"),
                            {"type": "unreachable", "dst": "default", "metric": 32767})
            self._add_route((self.physical.subnet, "dev", self.physical.interface,
                             "scope", "link", "src", self.physical.address),
                            {"dst": self.physical.subnet, "dev": self.physical.interface,
                             "scope": "link", "prefsrc": self.physical.address})
            self._add_route((str(self.network.spec.subnet), "dev", self.network.spec.interface,
                             "scope", "link", "src", self.network.spec.host),
                            {"dst": str(self.network.spec.subnet), "dev": self.network.spec.interface,
                             "scope": "link", "prefsrc": self.network.spec.host})
            self._add_route(("default", "via", self.physical.gateway, "dev", self.physical.interface,
                             "src", self.physical.address, "metric", "100"),
                            {"dst": "default", "gateway": self.physical.gateway,
                             "dev": self.physical.interface, "prefsrc": self.physical.address, "metric": 100})
            self.uncertain = True
            self._nft("-f", "-", input_data=self._script())
            self.firewall_snapshot, self.firewall_handle = self._firewall()
            self.uncertain = False
            # Publish source/destination rules last. Until all prior owned
            # resources are ready, the supervisor must not launch consumers.
            for priority, direction in ((outgoing, "from"), (returning, "to")):
                self.uncertain = True
                self._ip("rule", "add", "priority", str(priority), direction,
                         self.network.spec.guest + "/32", "table", str(self.table))
                rules = [row for row in self._rules() if row.get("priority") == priority]
                expected = {"priority": priority, "src": self.network.spec.guest if direction == "from" else "all",
                            "table": str(self.table)}
                if direction == "to":
                    expected["dst"] = self.network.spec.guest
                actual = dict(rules[0]) if len(rules) == 1 else {}
                if "table" in actual:
                    actual["table"] = str(_table(actual["table"]))
                for key in ("src", "dst"):
                    if key in actual and actual[key] != "all":
                        address = ipaddress.IPv4Network(actual[key], strict=False)
                        if address.prefixlen == 32:
                            actual[key] = str(address.network_address)
                if actual != expected:
                    raise NetworkError("The published profile routing rule could not be captured")
                self.rule_snapshots.append(rules[0])
                self.uncertain = False
            self.verify()
            return self
        except BaseException as error:
            try:
                self.close()
            except BaseException:
                raise ForwardingRetained(self) from error
            raise

    def close(self):
        self._guard()
        self.network._empty()
        try:
            self._verify_owned()
            while self.rule_snapshots:
                self._verify_owned()
                rule = self.rule_snapshots[-1]
                self.uncertain = True
                selector = (("from", str(ipaddress.IPv4Network(rule["src"], strict=False)))
                            if rule["src"] != "all" else
                            ("to", str(ipaddress.IPv4Network(rule["dst"], strict=False))))
                self._ip("rule", "delete", "priority", str(rule["priority"]), *selector, "table", str(self.table))
                if any(row.get("priority") == rule["priority"] for row in self._rules()):
                    raise NetworkError("The removed profile rule still has an owner")
                self.rule_snapshots.pop()
                self.uncertain = False
            if self.firewall_snapshot is not None:
                self.uncertain = True
                self._nft("delete", "table", "inet", "handle", str(self.firewall_handle))
                self.firewall_snapshot = None
                self.firewall_handle = None
                self.uncertain = False
            while self.route_records:
                self._verify_owned()
                args, row = self.route_records[-1]
                self.uncertain = True
                self._ip("route", "delete", *args, "table", str(self.table), "proto", "static")
                expected = list(self.route_snapshot)
                expected.remove(row)
                if _canonical(self._routes()) != _canonical(expected):
                    raise NetworkError("The remaining private routes changed during teardown")
                self.route_records.pop()
                self.route_snapshot = expected
                self.uncertain = False
        except BaseException as error:
            raise ForwardingRetained(self) from error
        self.closed = True
