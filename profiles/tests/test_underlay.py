# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.networks import NetworkError
from karyo_profiles.underlay import PhysicalUnderlay, discover_underlay, interface_name


class Inventory:
    def __init__(self):
        self.links = [{"ifname": "eth0", "ifindex": 2, "flags": ["UP", "LOWER_UP"]},
                      {"ifname": "wg0", "ifindex": 3, "flags": ["UP", "LOWER_UP"],
                       "linkinfo": {"info_kind": "wireguard"}}]
        self.routes = [{"dst": "default", "gateway": "192.0.2.1", "dev": "eth0",
                        "prefsrc": "192.0.2.2", "metric": 100},
                       {"dst": "default", "gateway": "10.0.0.1", "dev": "wg0", "metric": 1}]
        self.addresses = [{"ifindex": 2, "ifname": "eth0", "addr_info": [
            {"family": "inet", "scope": "global", "local": "192.0.2.2", "prefixlen": 24}]}]

    def run(self, *args):
        if args[-3:] == ("-d", "link", "show"):
            return json.dumps(self.links)
        if "route" in args:
            return json.dumps(self.routes)
        assert "address" in args
        return json.dumps(self.addresses)


class UnderlayTest(unittest.TestCase):
    def setUp(self):
        self.command = Inventory()

    def discover(self):
        return discover_underlay(self.command, physical=lambda name: name != "wg0")

    def test_lower_metric_vpn_is_not_the_physical_underlay(self):
        result = self.discover()
        self.assertEqual((result.interface, result.index, result.address, result.subnet, result.gateway),
                         ("eth0", 2, "192.0.2.2", "192.0.2.0/24", "192.0.2.1"))

    def test_no_physical_route_is_an_explicit_error(self):
        self.command.routes.pop(0)
        with self.assertRaises(NetworkError):
            self.discover()

    def test_down_or_enslaved_device_is_not_usable(self):
        for edit in ({"flags": ["UP"]}, {"master": "br0"}, {"linkinfo": {"info_kind": "veth"}}):
            old = self.command.links[0].copy()
            self.command.links[0].update(edit)
            with self.assertRaises(NetworkError):
                self.discover()
            self.command.links[0] = old

    def test_unknown_device_or_changed_address_identity_refused(self):
        self.command.routes[0]["dev"] = "missing"
        with self.assertRaises(NetworkError):
            self.discover()
        self.command.routes[0]["dev"] = "eth0"
        self.command.addresses[0]["ifindex"] = 44
        with self.assertRaises(NetworkError):
            self.discover()

    def test_ambiguous_addresses_and_equal_priority_gateways_refused(self):
        row = self.command.addresses[0]
        row["addr_info"].append(dict(row["addr_info"][0], local="192.0.2.3"))
        self.assertEqual(self.discover().address, "192.0.2.2")
        self.command.routes[0].pop("prefsrc")
        with self.assertRaises(NetworkError):
            self.discover()
        row["addr_info"].pop()
        self.command.links.append({"ifname": "eth1", "ifindex": 4, "flags": ["UP", "LOWER_UP"]})
        self.command.addresses.append({"ifname": "eth1", "ifindex": 4, "addr_info": row["addr_info"]})
        self.command.routes.append(dict(self.command.routes[0], dev="eth1"))
        with self.assertRaises(NetworkError):
            self.discover()
        self.command.routes[-1]["metric"] = 200
        self.assertEqual(self.discover().interface, "eth0")

    def test_multipath_expired_or_tentative_address_fails_closed(self):
        for flags in ({"flags": ["onlink"]}, {"multipath": []}, {"nexthops": []}):
            old = self.command.routes[0].copy()
            self.command.routes[0].update(flags)
            with self.assertRaises(NetworkError):
                self.discover()
            self.command.routes[0] = old
        for entry in ({"tentative": True}, {"valid_life_time": 0}, {"deprecated": True}):
            old = self.command.addresses[0]["addr_info"][0].copy()
            self.command.addresses[0]["addr_info"][0].update(entry)
            with self.assertRaises(NetworkError):
                self.discover()
            self.command.addresses[0]["addr_info"][0] = old

    def test_literal_interface_and_subnet_bounds(self):
        for name in ("../eth0", "a" * 16, "-x", "eth0\n", None):
            with self.assertRaises(NetworkError):
                interface_name(name)
        for field, value in (("index", True), ("prefix", 0), ("prefix", 31), ("metric", -1),
                             ("address", "127.0.0.1"), ("gateway", "192.0.3.1"),
                             ("gateway", "192.0.2.0"), ("gateway", "192.0.2.2")):
            values=dict(interface="eth0",index=2,address="192.0.2.2",prefix=24,gateway="192.0.2.1",metric=100)
            values[field]=value
            with self.assertRaises((NetworkError, ValueError)):
                PhysicalUnderlay(**values)


if __name__ == "__main__":
    unittest.main()
