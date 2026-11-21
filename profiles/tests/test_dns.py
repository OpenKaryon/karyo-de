# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import sys
import tempfile
import select
import subprocess
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.dns import DNSSeed, ResolverFiles, ResolverPlan, discover_dns, seed_private_dns, supported_resolver_layout, _domain
from karyo_profiles.networks import NetworkError, NetworkSpec
from karyo_profiles.storage import ConfigStore
from karyo_profiles.underlay import PhysicalUnderlay

PHYSICAL = PhysicalUnderlay("eth0", 2, "192.0.2.2", 24, "192.0.2.1", 100)
SEED = DNSSeed(PHYSICAL, ("192.0.2.1",), ("localdomain",))
SPEC = NetworkSpec(1001, "a" * 32)


class Bus:
    def __init__(self):
        self.rows = {"GetLink": ("o", ["/org/freedesktop/resolve1/link/_32"]),
                     "DNS": ("a(iay)", [[2, [192, 0, 2, 1]], [10, [32, 1] + [0] * 14]]),
                     "DNSOverTLS": ("s", "no"),
                     "GlobalDNSOverTLS": ("s", "no"),
                     "DNSEx": ("a(iayqs)", [[2, [192, 0, 2, 1], 0, ""]]),
                     "Domains": ("a(sb)", [["LocalDomain.", False], [".", True], ["work.invalid", True]])}
        self.calls = []

    def run(self, *args):
        self.calls.append(args)
        name = "GetLink" if "GetLink" in args else args[-1]
        if name == "DNSOverTLS" and "/org/freedesktop/resolve1" in args:
            name = "GlobalDNSOverTLS"
        signature, data = self.rows[name]
        return json.dumps({"type": signature, "data": data})


class DNSTest(unittest.TestCase):
    def setUp(self):
        self.bus = Bus()
        self.discovery = patch("karyo_profiles.dns.discover_underlay", return_value=PHYSICAL)
        self.discovery.start()
        self.addCleanup(self.discovery.stop)

    def test_physical_link_only_not_global_stub_or_routing_domains(self):
        self.assertEqual(discover_dns(PHYSICAL, self.bus), SEED)
        self.assertTrue(all("--system" in call and "--timeout=5s" in call for call in self.bus.calls))
        self.assertTrue(all("/org/freedesktop/resolve1/link/_32" in call for call in self.bus.calls[1:]))

    def test_signature_path_and_address_tuple_are_strict(self):
        for key, value in (("GetLink", ("s", ["/org/freedesktop/resolve1/link/_32"])),
                           ("GetLink", ("o", ["/other/path"])), ("DNS", ("a(iay)", [[True, [1,2,3,4]]])),
                           ("DNS", ("a(iay)", [[2, [127,0,0,53]]])), ("DNS", ("a(iay)", [[2,[1,2,3,256]]])),
                           ("DNS", ("a(iay)", [])), ("DNSOverTLS", ("s", "yes")), ("DNSOverTLS", ("s", ["no"])),
                           ("DNSEx", ("a(iayqs)", [[2, [192, 0, 2, 1], 853, "dns.example.org"]])), ("Domains", ("a(sb)", [["corp", 0]]))):
            old = self.bus.rows[key]
            self.bus.rows[key] = value
            with self.assertRaises((NetworkError, ValueError)):
                discover_dns(PHYSICAL, self.bus)
            self.bus.rows[key] = old

    def test_transport_property_is_scalar_and_empty_link_inherits_manager(self):
        self.bus.rows["DNSOverTLS"] = ("s", "")
        self.assertEqual(discover_dns(PHYSICAL, self.bus), SEED)
        self.bus.rows["GlobalDNSOverTLS"] = ("s", "yes")
        with self.assertRaises(NetworkError):
            discover_dns(PHYSICAL, self.bus)

    def test_changing_physical_link_is_not_a_successful_snapshot(self):
        with patch("karyo_profiles.dns.discover_underlay", side_effect=[PHYSICAL, NetworkError("changed")]):
            with self.assertRaises(NetworkError):
                discover_dns(PHYSICAL, self.bus)

    def test_immutable_server_and_search_bounds(self):
        for servers in ([], (), ("127.0.0.53",), ("10.231.0.2",), ("0.0.0.0",), ("224.0.0.1",),
                        ("192.0.2.1", "192.0.2.1"), ("1.1.1.1",) * 17):
            with self.assertRaises((NetworkError, ValueError)):
                DNSSeed(PHYSICAL, servers)
        for value in ("hello\nnameserver 1.1.1.1", "x;exit", "-x.test", "a..b", "a" * 64, "тест.рф"):
            with self.assertRaises(NetworkError):
                _domain(value)

    def test_private_plan_keeps_gui_host_bus_and_normal_sudo(self):
        plan = ResolverPlan(SPEC, SEED, 1001, 990, "/run/karyo/resolver-config/" + SPEC.token)
        client = plan.client_properties()
        self.assertEqual(len(client), 3)
        self.assertFalse(any(any(word in item for word in ("DBUS_SYSTEM", "NoNewPrivileges", "Capability")) for item in client))
        self.assertIn("/run/systemd/resolve", client[1])
        self.assertTrue(any("DBUS_SYSTEM_BUS_ADDRESS=" + plan.bus_address in item for item in plan.resolver_properties()))
        root = ET.fromstring(plan.files()["bus.conf"])
        profile = root.find("./policy[@user='1001']")
        self.assertIsNotNone(profile)
        self.assertFalse(any(item.get("own") for item in profile))
        self.assertFalse(any(item.get("send_member", "").startswith("Set") for item in profile))
        self.assertIn(b"FallbackDNS=\n", plan.files()["resolved.conf"])

    def test_plan_rejects_property_and_xml_injection(self):
        for key, value in (("gid", 0), ("gid", True), ("service_uid", 1001),
                           ("config", "/tmp/a b"), ("config", "/run/x:other"), ("config", "/run/../etc")):
            values = dict(spec=SPEC, seed=SEED, gid=1001, service_uid=990, config="/run/karyo/test")
            values[key] = value
            with self.assertRaises(NetworkError):
                ResolverPlan(**values)


@unittest.skipUnless(os.geteuid() == 0, "root-owned resolver file lifecycle")
class ResolverFilesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="karyo-dns-", dir="/root")
        self.addCleanup(self.temp.cleanup)
        self.store = ConfigStore(runtime=Path(self.temp.name) / "runtime")
        self.held = self.store.locked()
        self.held.__enter__()
        self.addCleanup(self.held.__exit__, None, None, None)
        def verify():
            if not self.store._locked or os.geteuid() != 0:
                raise PermissionError("root fixture lock required")
        self.network = SimpleNamespace(spec=SPEC, store=self.store, verify=verify, _empty=lambda: None)
        self.accounts = SimpleNamespace(by_uid=lambda uid: SimpleNamespace(uid=uid, gid=1001))
        self.layout = patch("karyo_profiles.dns.supported_resolver_layout", return_value="stub-resolv.conf")
        self.layout.start()
        self.addCleanup(self.layout.stop)
        self.service = patch("karyo_profiles.dns.resolver_service_uid", return_value=990)
        self.service.start()
        self.addCleanup(self.service.stop)
        self.files = ResolverFiles(self.network, SEED, self.accounts)
        self.addCleanup(self.close_fd)

    def close_fd(self):
        if self.files.fd is not None:
            os.close(self.files.fd)
            self.files.fd = None

    def test_native_layout_variants_and_unsupported_files(self):
        self.layout.stop()
        path = Path(self.temp.name) / "resolv.conf"
        for target in ("/run/systemd/resolve/stub-resolv.conf", "../run/systemd/resolve/resolv.conf"):
            path.symlink_to(target)
            self.assertIn(supported_resolver_layout(path), {"stub-resolv.conf", "resolv.conf"})
            path.unlink()
        path.write_text("nameserver 127.0.0.53")
        with self.assertRaises(NetworkError):
            supported_resolver_layout(path)
        path.unlink()
        path.symlink_to("/run/NetworkManager/resolv.conf")
        with self.assertRaises(NetworkError):
            supported_resolver_layout(path)
        self.layout.start()

    def test_unsupported_layout_refuses_before_any_configuration_directory(self):
        with patch("karyo_profiles.dns.supported_resolver_layout", side_effect=NetworkError("unsupported layout")):
            with self.assertRaises(NetworkError):
                self.files.create()
        self.assertFalse(Path(self.files.plan.config).exists())

    def test_create_capture_and_close_only_unchanged_owned_files(self):
        self.files.create()
        self.assertFalse(os.get_inheritable(self.files.fd))
        self.assertEqual(set(os.listdir(self.files.fd)), {"resolved.conf", "bus.conf"})
        directory = Path(self.files.plan.config)
        self.files.close()
        self.assertFalse(directory.exists())
        with self.assertRaises(NetworkError):
            self.files.close()

    def test_existing_directory_is_never_adopted_or_removed(self):
        directory = Path(self.files.plan.config)
        directory.mkdir(parents=True)
        (directory / "foreign").write_text("keep")
        with self.assertRaises(FileExistsError):
            self.files.create()
        self.assertEqual((directory / "foreign").read_text(), "keep")

    def test_unknown_file_and_live_runtime_refuse_every_unlink(self):
        self.files.create()
        directory = Path(self.files.plan.config)
        (directory / "foreign").write_text("keep")
        with self.assertRaises(NetworkError):
            self.files.close()
        self.assertEqual(len(list(directory.iterdir())), 3)
        (directory / "foreign").unlink()
        with patch("karyo_profiles.dns.os.path.lexists", return_value=True):
            with self.assertRaises(NetworkError):
                self.files.close()
        self.assertEqual(len(list(directory.iterdir())), 2)
        self.files.close()

    def test_replacement_symlink_hardlink_and_payload_change_refused(self):
        self.files.create()
        directory = Path(self.files.plan.config)
        file = directory / "bus.conf"
        original = file.read_bytes()
        file.write_bytes(original + b"changed")
        with self.assertRaises(NetworkError):
            self.files.close()
        file.write_bytes(original)
        link = directory.parent / "extra"
        os.link(file, link)
        with self.assertRaises(NetworkError):
            self.files.close()
        link.unlink()
        backup = directory.parent / "original"
        file.rename(backup)
        file.symlink_to(backup)
        with self.assertRaises(OSError):
            self.files.close()
        file.unlink()
        backup.rename(file)
        self.files.close()

    def test_native_dbus_accepts_configuration_and_root_can_connect(self):
        self.files.create()
        address = "unix:path=" + self.temp.name + "/bus"
        conf = Path(self.temp.name) / "native-bus.conf"
        conf.write_bytes(self.files.plan.files()["bus.conf"].replace(self.files.plan.bus_address.encode(), address.encode()))
        process = subprocess.Popen(["/usr/bin/dbus-daemon", "--nofork", "--nosyslog", "--nopidfile",
                                    "--config-file=" + str(conf), "--print-address"],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.assertTrue(select.select([process.stdout], [], [], 3)[0])
            self.assertTrue(process.stdout.readline().decode().startswith(address))
            result = subprocess.run(["/usr/bin/busctl", "--address=" + address, "--timeout=2s", "call",
                                     "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus.Peer", "Ping"],
                                    capture_output=True, timeout=3)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
        finally:
            if process.poll() is None:
                process.terminate()
            _, diagnostic = process.communicate(timeout=3)
            self.assertNotIn(b"Rejected", diagnostic, diagnostic.decode())
        self.files.close()

    def test_private_bootstrap_never_uses_host_system_bus(self):
        calls = []
        def run(*args):
            calls.append(args)
            return json.dumps({"type": "o", "data": ["/org/freedesktop/resolve1/link/_32"]}) if "GetLink" in args else ""
        self.network.command = SimpleNamespace(run=run)
        self.network._inventory = lambda inside: {"eth0": {"ifindex": 2}}
        self.files.create()
        seed_private_dns(self.files)
        self.assertEqual(len(calls), 4)
        self.assertTrue(all("--address=" + self.files.plan.bus_address in args and "--system" not in args for args in calls))
        self.assertEqual(calls[1][-8:], ("a(iay)", "1", "2", "4", "192", "0", "2", "1"))
        self.assertIn("SetDefaultRoute", calls[3])
        self.files.close()

    def test_private_bootstrap_rejects_unknown_link_before_setting_dns(self):
        self.network.command = SimpleNamespace(run=lambda *args: json.dumps({"type": "o", "data": ["/foreign"]}))
        self.network._inventory = lambda inside: {"eth0": {"ifindex": 2}}
        self.files.create()
        with self.assertRaises(NetworkError):
            seed_private_dns(self.files)
        self.files.close()

    def test_released_lock_and_active_consumers_prevent_cleanup(self):
        self.files.create()
        self.store._locked = False
        with self.assertRaises(PermissionError):
            self.files.close()
        self.store._locked = True
        self.network._empty = lambda: (_ for _ in ()).throw(NetworkError("live consumer"))
        with self.assertRaises(NetworkError):
            self.files.close()
        self.network._empty = lambda: None
        self.files.close()


if __name__ == "__main__":
    unittest.main()
