# SPDX-License-Identifier: GPL-3.0-or-later
import copy
from dataclasses import replace
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.forwarding import (ForwardingRetained, ProfileForwarding, choose_priority, _table)
from karyo_profiles.networks import NetworkCommand, NetworkError, NetworkSpec
from karyo_profiles.underlay import PhysicalUnderlay

PHYSICAL = PhysicalUnderlay("eth0", 2, "192.0.2.2", 24, "192.0.2.1", 100)


class Kernel:
    def __init__(self, spec):
        self.spec = spec
        self.routes = []
        self.rules = [{"priority": 0, "src": "all", "table": "255"},
                      {"priority": 100, "src": "all", "table": "7120"},
                      {"priority": 32766, "src": "all", "table": "254"}]
        self.firewall = None
        self.calls = []
        self.fail = None

    def run(self, *args, input_data=None):
        self.calls.append((args, input_data))
        if self.fail and self.fail(args):
            raise TimeoutError("native acknowledgement lost")
        if args[0] == "/usr/sbin/nft":
            if args[1:] == ("-j", "list", "tables"):
                rows = [] if self.firewall is None else [self.firewall[0]]
                return json.dumps({"nftables": rows})
            if args[1:4] == ("-j", "list", "table"):
                return json.dumps({"nftables": self.firewall})
            if args[1:] == ("-f", "-"):
                assert input_data and len(input_data) <= 4096
                self.firewall = [{"table": {"family": "inet", "name": "karyo_fwd_" + self.spec.token,
                                           "comment": self.spec.alias, "handle": 50}},
                                 {"rule": {"family": "inet", "table": "karyo_fwd_" + self.spec.token,
                                           "handle": 51, "expr": [{"accept": None}]}}]
            if args[1:4] == ("delete", "table", "inet"):
                assert args[-1] == "50"
                self.firewall = None
            return ""
        assert args[:2] == ("/usr/sbin/ip", "-4")
        arguments = args[2:]
        if arguments[:4] == ("-j", "-N", "route", "show"):
            return json.dumps(self.routes)
        if arguments == ("-j", "-N", "rule", "show"):
            return json.dumps(self.rules)
        command, operation, *tail = arguments
        def value(key):
            return tail[tail.index(key) + 1]
        if command == "route":
            row = {"dst": tail[1] if tail[0] == "unreachable" else tail[0],
                   "table": value("table"), "protocol": "4", "flags": []}
            if tail[0] == "unreachable":
                row["type"] = "7"
            for key, field in (("metric", "metric"), ("dev", "dev"), ("via", "gateway"),
                               ("src", "prefsrc"), ("scope", "scope")):
                if key in tail:
                    row[field] = int(value(key)) if key == "metric" else value(key)
            if operation == "add":
                self.routes.append(row)
            else:
                self.routes.remove(row)
        elif command == "rule":
            row = {"priority": int(value("priority")), "src": value("from").removesuffix("/32") if "from" in tail else "all",
                   "table": value("table")}
            if "to" in tail:
                row["dst"] = value("to").removesuffix("/32")
            if operation == "add":
                self.rules.append(row)
                self.rules.sort(key=lambda row: row["priority"])
            else:
                self.rules.remove(row)
        else:
            raise AssertionError(args)
        return ""


class ForwardingTest(unittest.TestCase):
    def setUp(self):
        self.spec = NetworkSpec(1002, "a" * 32)
        self.kernel = Kernel(self.spec)
        self.empty = True
        self.locked = True
        def verify():
            if not self.locked:
                raise PermissionError("fixture configuration lock released")
        def empty():
            if not self.empty:
                raise NetworkError("fixture namespace consumers still running")
        self.network = SimpleNamespace(spec=self.spec, command=self.kernel, verify=verify, _empty=empty)
        self.gateway = ProfileForwarding(self.network)
        for item in (patch("karyo_profiles.forwarding.discover_underlay", return_value=PHYSICAL),
                     patch("karyo_profiles.forwarding.Path.read_text", return_value="1\n")):
            item.start()
            self.addCleanup(item.stop)

    def deletes(self):
        return [args for args, data in self.kernel.calls if "delete" in args]

    def test_gateway_pins_both_directions_and_deletes_only_captured_objects(self):
        original = copy.deepcopy(self.kernel.rules)
        self.gateway.create().verify()
        self.assertEqual(self.gateway.priorities, [1, 17])
        self.assertEqual(len(self.kernel.routes), 4)
        self.assertEqual([row["src"] for row in self.gateway.rule_snapshots], [self.spec.guest, "all"])
        self.assertEqual(self.gateway.rule_snapshots[1]["dst"], self.spec.guest)
        self.assertIn(b"masquerade", self.gateway._script())
        self.assertIn(b"ip daddr 10.231.0.10 oifname !=", self.gateway._script())
        self.gateway.close()
        self.assertEqual(self.kernel.rules, original)
        self.assertFalse(self.kernel.routes)
        self.assertIsNone(self.kernel.firewall)
        self.assertEqual(len(self.deletes()), 7)

    def test_metric_change_on_the_same_selected_path_keeps_owned_forwarding(self):
        self.gateway.create()
        before = copy.deepcopy((self.kernel.routes, self.kernel.rules, self.kernel.firewall))
        with patch("karyo_profiles.forwarding.discover_underlay", return_value=replace(PHYSICAL, metric=99)):
            self.gateway.verify()
        self.assertEqual((self.kernel.routes, self.kernel.rules, self.kernel.firewall), before)
        self.assertFalse(self.deletes())
        self.gateway.close()

    def test_changed_selected_path_still_requires_reconciliation(self):
        self.gateway.create()
        for change in (dict(interface="eth1"), dict(index=3), dict(address="192.0.2.3"),
                       dict(prefix=25), dict(gateway="192.0.2.10")):
            with self.subTest(change=change), patch("karyo_profiles.forwarding.discover_underlay",
                    return_value=replace(PHYSICAL, **change)):
                with self.assertRaisesRegex(NetworkError, "physical network changed"):
                    self.gateway.verify()
        self.assertFalse(self.deletes())
        self.gateway.close()

    def test_no_implicit_global_forwarding_and_no_active_consumers(self):
        with patch("karyo_profiles.forwarding.Path.read_text", return_value="0\n"):
            with self.assertRaises(NetworkError):
                self.gateway.create()
        self.empty = False
        with self.assertRaises(NetworkError):
            self.gateway.create()
        self.assertFalse(self.kernel.calls)

    def test_config_lock_is_required_for_setup_and_teardown(self):
        self.locked = False
        with self.assertRaises(PermissionError):
            self.gateway.create()
        self.locked = True
        self.gateway.create()
        self.locked = False
        with self.assertRaises(PermissionError):
            self.gateway.close()
        self.assertFalse(self.deletes())
        self.locked = True
        self.gateway.close()

    def test_foreign_route_table_rule_or_firewall_causes_read_only_refusal(self):
        self.kernel.routes.append({"table": "40002", "dst": "default"})
        with self.assertRaises(NetworkError):
            self.gateway.create()
        self.kernel.routes.clear()
        self.kernel.rules.append({"priority": 200, "src": "all", "table": 40002})
        with self.assertRaises(NetworkError):
            self.gateway.create()
        self.kernel.rules.pop()
        self.kernel.firewall = [{"table": {"name": self.gateway.name, "family": "inet", "comment": "foreign"}}]
        with self.assertRaises(NetworkError):
            self.gateway.create()
        self.assertFalse(self.gateway.attempted)
        self.assertFalse(self.deletes())

    def test_changed_rule_route_or_firewall_contents_refuse_all_cleanup(self):
        self.gateway.create()
        for what in ("route", "rule", "firewall", "handle"):
            original = copy.deepcopy((self.kernel.routes, self.kernel.rules, self.kernel.firewall))
            if what == "route":
                self.kernel.routes[0]["metric"] = 42
            elif what == "rule":
                self.kernel.rules[1]["fwmark"] = "0x1"
            elif what == "firewall":
                self.kernel.firewall.append({"rule": {"foreign": True}})
            else:
                self.kernel.firewall[0]["table"]["handle"] = 99
            with self.assertRaises(ForwardingRetained):
                self.gateway.close()
            self.assertFalse(self.deletes())
            self.kernel.routes, self.kernel.rules, self.kernel.firewall = original
        self.gateway.close()

    def test_acknowledged_late_failure_rolls_back_its_complete_graph(self):
        with patch("karyo_profiles.forwarding.discover_underlay", side_effect=[PHYSICAL, NetworkError("changed")]):
            with self.assertRaises(NetworkError):
                self.gateway.create()
        self.assertTrue(self.gateway.closed)
        self.assertFalse(self.kernel.routes)
        self.assertIsNone(self.kernel.firewall)
        self.assertEqual(len(self.deletes()), 7)

    def test_unacknowledged_mutation_is_retained_and_cannot_be_recreated(self):
        self.kernel.fail = lambda args: args[2:4] == ("route", "add")
        with self.assertRaises(ForwardingRetained) as raised:
            self.gateway.create()
        self.assertIs(raised.exception.gateway, self.gateway)
        self.assertTrue(self.gateway.uncertain)
        with self.assertRaises(NetworkError):
            self.gateway.create()
        self.assertFalse(self.deletes())

    def test_live_consumer_prevents_teardown(self):
        self.gateway.create()
        self.empty = False
        with self.assertRaises(NetworkError):
            self.gateway.close()
        self.assertFalse(self.deletes())
        self.empty = True
        self.gateway.close()

    def test_a_new_foreign_reference_to_private_table_prevents_teardown(self):
        self.gateway.create()
        self.kernel.rules.append({"priority": 77, "src": "all", "table": "40002"})
        with self.assertRaises(ForwardingRetained):
            self.gateway.close()
        self.assertFalse(self.deletes())
        self.kernel.rules.pop()
        self.gateway.close()

    def test_numeric_scope_and_table_identity_survive_native_normalization(self):
        self.gateway.create()
        for row in self.kernel.routes:
            if row.get("scope") == "link":
                row["scope"] = "253"
        self.gateway.verify()
        # Fixture delete emulates kernel equivalence of symbolic/numeric scope.
        for row in self.kernel.routes:
            if row.get("scope") == "253":
                row["scope"] = "link"
        self.gateway.close()


class PriorityTest(unittest.TestCase):
    def test_no_priority_before_a_foreign_global_rule(self):
        rows = [{"priority": 0, "src": "all", "table": "255"},
                {"priority": 1, "src": "all", "table": "7120"}]
        with self.assertRaises(NetworkError):
            choose_priority(rows, "kp000")
        rows[1]["iif"] = "other"
        self.assertEqual(choose_priority(rows, "kp000"), 2)
        rows[1]["not"] = True
        with self.assertRaises(NetworkError):
            choose_priority(rows, "kp000")

    def test_all_early_slots_or_malformed_priorities_fail_closed(self):
        for rows in ([{"priority": True}], [{"priority": -1}],
                     [{"priority": n, "src": "all", "table": "254"} for n in range(1, 65)]):
            with self.assertRaises(NetworkError):
                choose_priority(rows, "kp000")
        for value in (True, "main", "040002", "x", 0, -1, None):
            with self.assertRaises(NetworkError):
                _table(value)

    def test_multiple_profile_rules_and_reverse_path_lookup_selectors(self):
        import ipaddress
        rows = [{"priority": 0, "src": "all", "table": "255"},
                {"priority": 1, "src": "10.231.0.10", "table": "40002"},
                {"priority": 17, "src": "all", "dst": "10.231.0.10", "table": "40002"},
                {"priority": 100, "src": "all", "table": "7120"}]
        private = ipaddress.IPv4Network("10.231.0.14/32")
        outgoing = choose_priority(rows, None, source=private, slots=range(1, 17))
        self.assertEqual(outgoing, 2)
        rows.append({"priority": 2, "src": "10.231.0.14", "table": "40003"})
        self.assertEqual(choose_priority(rows, None, private, slots=range(17, 33),
                                         exclude_private_sources=True), 18)
        # A foreign early global/loopback policy must not be silently bypassed
        # by assuming it cannot affect the kernel's reverse-path lookup.
        rows.append({"priority": 9, "src": "all", "iif": "lo", "table": "8123"})
        with self.assertRaises(NetworkError):
            choose_priority(rows, None, private, slots=range(17, 33), exclude_private_sources=True)

    def test_bounded_stdin_and_actual_pipe(self):
        runner = NetworkCommand()
        self.assertEqual(runner.run("/usr/bin/cat", input_data=b"x" * 4096), "x" * 4096)
        for data in (b"x" * 4097, "text"):
            with self.assertRaises(ValueError):
                runner.run("/usr/bin/cat", input_data=data)


if __name__ == "__main__":
    unittest.main()
