# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.networks import (NetworkCommand, NetworkError, NetworkRetained,
                                    NetworkSpec, ProfileNetwork, _links)


class Kernel:
    def __init__(self, spec):
        self.spec = spec
        self.host = {}
        self.inside = {"lo": {"ifname": "lo", "ifindex": 1}}
        self.routes = []
        self.calls = []
        self.pids = ""
        self.failure = None
        self.identity = (4, 100)
        self.present = False

    def run(self, *arguments):
        self.calls.append(arguments)
        if self.failure and self.failure(arguments):
            raise TimeoutError("native fixture failure")
        inside = arguments[1:3] == ("-n", self.spec.name)
        args = arguments[3:] if inside else arguments[1:]
        if args == ("-j", "-d", "link", "show"):
            return json.dumps(list((self.inside if inside else self.host).values()))
        if args == ("-4", "-j", "route", "show", "table", "all"):
            return json.dumps(self.routes)
        if args[:2] == ("netns", "pids"):
            return self.pids
        if args[:2] == ("netns", "add"):
            self.present = True
        if args[:2] == ("link", "add"):
            for mapping, name, index, peer in ((self.host, self.spec.interface, 9, 2),
                                               (self.inside, "eth0", 2, 9)):
                mapping[name] = {"ifname": name, "ifindex": index, "link_index": peer,
                                 "linkinfo": {"info_kind": "veth"}}
        if args[:2] == ("link", "set") and "alias" in args:
            (self.inside if inside else self.host)[args[3]]["ifalias"] = args[-1]
        if args[:2] == ("link", "set") and args[-1] == "up":
            (self.inside if inside else self.host)[args[3]]["flags"] = ["UP"]
        if args[:3] == ("route", "add", "default") and inside:
            if "UP" not in self.inside["eth0"].get("flags", []):
                raise NetworkError("native gateway is unreachable while eth0 is down")
        if args[:2] == ("link", "delete"):
            self.host.pop(self.spec.interface)
            self.inside.pop("eth0")
        if args[:2] == ("netns", "delete"):
            self.present = False
        return ""


class Lease(ProfileNetwork):
    def _pin(self):
        if not self.command.present:
            raise FileNotFoundError(self.path)
        return os.open(os.devnull, os.O_RDONLY | os.O_CLOEXEC), self.command.identity


class NetworkTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.patches = (
            patch("karyo_profiles.networks.os.getuid", return_value=0),
            patch("karyo_profiles.networks.os.geteuid", return_value=0),
            patch("karyo_profiles.networks.trusted_directory",
                  side_effect=lambda *a, **k: os.open(self.directory.name, os.O_RDONLY)),
        )
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)
        self.store = SimpleNamespace(_locked=True)
        self.spec = NetworkSpec(1002, "ab" * 16)
        self.kernel = Kernel(self.spec)
        self.lease = Lease(self.store, self.spec, self.kernel)
        self.addCleanup(self.release_fd)

    def release_fd(self):
        if self.lease.fd is not None:
            os.close(self.lease.fd)
            self.lease.fd = None

    def deletes(self):
        return [call for call in self.kernel.calls if "delete" in call]

    def test_pool_bounds_and_literal_tokens(self):
        self.assertEqual(str(self.spec.subnet), "10.231.0.8/30")
        self.assertEqual((self.spec.host, self.spec.guest), ("10.231.0.9", "10.231.0.10"))
        self.assertEqual(str(NetworkSpec(17383, "a" * 32).subnet), "10.231.255.252/30")
        for uid in (True, "1002", 999, 17384):
            with self.assertRaises(ValueError):
                NetworkSpec(uid, "a" * 32)
        for token in (None, "A" * 32, "a" * 31, "x/../a", "a" * 32 + "\n"):
            with self.assertRaises(ValueError):
                NetworkSpec(1002, token)

    def test_success_owns_pair_and_closes_only_after_consumers_stop(self):
        self.assertIs(self.lease.create(), self.lease)
        fd = self.lease.fd
        self.assertFalse(os.get_inheritable(fd))
        self.lease.verify()
        self.kernel.pids = "987\n"
        with self.assertRaises(NetworkRetained):
            self.lease.close()
        self.assertFalse(self.deletes())
        self.kernel.pids = ""
        self.lease.close()
        self.assertFalse(self.kernel.present)
        self.assertEqual(len(self.deletes()), 2)
        with self.assertRaises(OSError):
            os.fstat(fd)
        with self.assertRaises(NetworkError):
            self.lease.verify()
        with self.assertRaises(NetworkError):
            self.lease.close()

    def test_lease_requires_real_root_and_config_lock(self):
        for attribute in ("getuid", "geteuid"):
            with patch("karyo_profiles.networks.os." + attribute, return_value=1001):
                with self.assertRaises(PermissionError):
                    self.lease.create()
        self.store._locked = False
        with self.assertRaises(PermissionError):
            self.lease.create()
        self.assertFalse(self.kernel.calls)

    def test_name_link_and_route_collisions_never_delete_existing_objects(self):
        path = Path(self.directory.name) / self.spec.name
        path.symlink_to("missing")
        with self.assertRaises(NetworkError):
            self.lease.create()
        path.unlink()
        self.kernel.host[self.spec.interface] = {"ifname": self.spec.interface, "ifindex": 5}
        with self.assertRaises(NetworkError):
            self.lease.create()
        self.kernel.host.clear()
        for destination in ("10.231.0.8/30", "10.0.0.0/8", "10.231.0.9"):
            self.kernel.routes = [{"dst": destination}]
            with self.assertRaises(NetworkError):
                self.lease.create()
        self.assertFalse(self.deletes())
        self.assertFalse(self.kernel.present)

    def test_default_route_does_not_consume_private_address_pool(self):
        self.kernel.routes = [{"dst": "default"}, {"dst": "0.0.0.0/0"}, {"dst": "192.168.0.0/16"}]
        self.lease.create().close()

    def test_namespace_replacement_keeps_held_descriptor_and_all_objects(self):
        self.lease.create()
        old = self.kernel.identity
        self.kernel.identity = (4, 101)
        with self.assertRaises(NetworkRetained):
            self.lease.close()
        self.assertFalse(self.deletes())
        self.assertIsNotNone(self.lease.fd)
        self.kernel.identity = old
        self.lease.close()

    def test_unknown_link_alias_index_type_or_master_refuses_teardown(self):
        self.lease.create()
        for mapping, name in ((self.kernel.host, self.spec.interface), (self.kernel.inside, "eth0")):
            for key, value in (("ifalias", "foreign"), ("ifindex", 88), ("link_index", 99),
                               ("linkinfo", {"info_kind": "dummy"}), ("master", "br0")):
                old = mapping[name].copy()
                mapping[name][key] = value
                with self.assertRaises(NetworkRetained):
                    self.lease.close()
                self.assertFalse(self.deletes())
                mapping[name] = old
        self.kernel.inside["extra"] = {"ifname": "extra", "ifindex": 17}
        with self.assertRaises(NetworkRetained):
            self.lease.close()
        self.assertFalse(self.deletes())
        self.kernel.inside.pop("extra")
        self.lease.close()

    def test_acknowledged_setup_failure_rolls_back_proven_pair(self):
        self.kernel.failure = lambda args: args[0] == "/usr/bin/nsenter"
        with self.assertRaises(TimeoutError):
            self.lease.create()
        self.assertTrue(self.lease.closed)
        self.assertFalse(self.kernel.present)
        self.assertEqual(len(self.deletes()), 2)

    def test_aliases_are_set_after_capture_and_failure_rolls_back_known_pair(self):
        self.kernel.failure = lambda a: a[1:3] == ("-n", self.spec.name) and "alias" in a
        with self.assertRaises(TimeoutError):
            self.lease.create()
        self.assertEqual(self.lease.tagged, {"host"})
        self.assertTrue(self.lease.closed)
        self.assertEqual(len(self.deletes()), 2)

    def test_unacknowledged_alias_change_is_retained_if_it_took_effect(self):
        original = self.kernel.run
        def run(*args):
            output = original(*args)
            if "alias" in args:
                raise TimeoutError("acknowledgement lost after alias changed")
            return output
        self.kernel.run = run
        with self.assertRaises(NetworkRetained):
            self.lease.create()
        self.assertEqual(self.lease.indices, (9, 2))
        self.assertFalse(self.deletes())
        self.assertFalse(self.lease.tagged)
        self.kernel.host[self.spec.interface].pop("ifalias")
        self.kernel.run = original
        self.lease.close()

    def test_unacknowledged_native_mutations_are_retained(self):
        for operation in ("namespace", "pair"):
            kernel = Kernel(self.spec)
            lease = Lease(self.store, self.spec, kernel)
            kernel.failure = lambda a: (a[1:3] == ("netns", "add") if operation == "namespace"
                                        else a[1:3] == ("link", "add"))
            try:
                with self.assertRaises(NetworkRetained) as raised:
                    lease.create()
                self.assertIs(raised.exception.lease, lease)
                self.assertFalse(any("delete" in call for call in kernel.calls))
                with self.assertRaises(NetworkError):
                    lease.create()
                self.assertEqual(lease.fd is not None, operation == "pair")
            finally:
                if lease.fd is not None:
                    os.close(lease.fd)

    def test_retry_after_pair_removal_never_removes_a_replacement_link(self):
        self.lease.create()
        self.kernel.failure = lambda a: a[1:3] == ("netns", "delete")
        with self.assertRaises(NetworkRetained):
            self.lease.close()
        self.assertIsNone(self.lease.indices)
        self.assertFalse(self.lease.pair_attempted)
        self.kernel.failure = None
        self.kernel.inside["foreign"] = {"ifname": "foreign", "ifindex": 23}
        with self.assertRaises(NetworkRetained):
            self.lease.close()
        self.assertTrue(self.kernel.present)
        self.kernel.inside.pop("foreign")
        self.lease.close()
        self.assertEqual(sum("link" in c and "delete" in c for c in self.kernel.calls), 1)

    def test_malformed_inventory_fails_without_mutation(self):
        for text in ("{}", "[1]", '[{"ifname":"eth0","ifindex":true}]',
                     '[{"ifname":"lo","ifindex":1},{"ifname":"lo","ifindex":2}]'):
            with self.assertRaises(NetworkError):
                _links(text)


class NativeCommandTest(unittest.TestCase):
    def test_bounded_native_output_and_fixed_environment(self):
        runner = NetworkCommand()
        self.assertEqual(runner.run("/usr/bin/printenv", "LANG"), "C.UTF-8\n")
        with self.assertRaises(NetworkError):
            runner.run("/usr/bin/false")
        with self.assertRaises(NetworkError):
            runner.run("/usr/bin/head", "-c", "262145", "/dev/zero")
        with self.assertRaises(UnicodeDecodeError):
            runner.run("/usr/bin/printf", "\\377")


if __name__ == "__main__":
    unittest.main()
