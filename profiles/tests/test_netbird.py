# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.netbird import NetBirdScope, NetBirdState
from karyo_profiles.networks import NetworkSpec
from karyo_profiles.services import OwnedUnit, ServiceError

ACCOUNT = LocalAccount("work", 1002, 1002, "/home/work", "/bin/bash")


def files():
    value = Mock()
    value.accounts.by_uid.return_value = ACCOUNT
    value.network.spec = NetworkSpec(1002, "ab" * 16)
    value.network.identity = (4, 5)
    value.network.store = SimpleNamespace(_locked=True)
    value.network.path = "/run/netns/" + value.network.spec.name
    value.plan.gid = 1002
    value.plan.bus_address = "unix:path=/run/karyo-resolve-bus-" + "ab" * 16 + "/bus"
    value.plan.runtime_name = "karyo-resolve-" + "ab" * 16
    return value


class NetBirdPlanTest(unittest.TestCase):
    def test_private_service_uses_private_bus_and_state_not_host_defaults(self):
        dns = Mock(files=files())
        scope = NetBirdScope(dns)
        values = dict(row.split("=", 1) for row in scope.properties())
        self.assertEqual(scope.unit.program, "/usr/bin/netbird")
        self.assertEqual(scope.unit.uid, 0)
        self.assertEqual(values["NetworkNamespacePath"], scope.network.path)
        self.assertIn(dns.files.plan.bus_address, values["Environment"])
        self.assertIn("NB_STATE_DIR=/var/lib/karyo/profile-vpn/1002/netbird", values["Environment"])
        self.assertIn("/run/dbus/system_bus_socket", values["InaccessiblePaths"])
        self.assertEqual(values["CapabilityBoundingSet"], "CAP_NET_ADMIN CAP_NET_RAW CAP_NET_BIND_SERVICE")
        self.assertEqual(values["Group"], "1002")
        self.assertEqual(values["RuntimeDirectoryMode"], "0750")
        self.assertEqual(scope.address, "unix:///run/karyo-netbird-" + "ab" * 16 + "/control.sock")
        OwnedUnit.properties(scope.properties())

    def test_provider_role_cannot_run_as_an_ordinary_account(self):
        with self.assertRaises(ValueError):
            OwnedUnit(SimpleNamespace(_locked=True), NetworkSpec(1002, "ab" * 16), "netbird", 1002)


@unittest.skipUnless(os.geteuid() == 0, "native root-owned state fixtures")
class NetBirdStateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="karyo-netbird-test-", dir="/run")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.files = files()
        self.home = patch.object(NetBirdState, "capture_home", return_value=(11, 22))
        self.home_mock = self.home.start()
        self.addCleanup(self.home.stop)
        self.state = NetBirdState(self.files, self.root)
        self.addCleanup(self.state.release_handles)

    def test_state_is_private_reusable_and_not_removed_by_handle_release(self):
        self.state.prepare()
        identity = self.state.identity
        self.assertEqual(self.state.path.stat().st_mode & 0o777, 0o700)
        self.assertEqual(json.loads((self.state.path / "owner.json").read_text())["account"]["uid"], 1002)
        (self.state.path / "config.json").write_text("synthetic fixture state")
        self.state.release_handles()
        again = NetBirdState(self.files, self.root)
        try:
            again.prepare()
            self.assertEqual(again.identity, identity)
            self.assertEqual((again.path / "config.json").read_text(), "synthetic fixture state")
        finally:
            again.release_handles()

    def test_existing_unstamped_directory_is_not_adopted_or_removed(self):
        self.state.path.mkdir(parents=True, mode=0o700)
        unexpected = self.state.path / "foreign"
        unexpected.write_text("unowned")
        with self.assertRaises(FileNotFoundError):
            self.state.prepare()
        self.assertEqual(unexpected.read_text(), "unowned")
        self.assertFalse((self.state.path / "owner.json").exists())

    def test_public_or_symlink_state_is_rejected_without_chown(self):
        self.state.path.mkdir(parents=True, mode=0o755)
        with self.assertRaises(ServiceError):
            self.state.prepare()
        self.assertEqual(self.state.path.stat().st_mode & 0o777, 0o755)
        self.state.path.rmdir()
        target = self.root / "foreign"
        target.mkdir()
        self.state.path.symlink_to(target, target_is_directory=True)
        with self.assertRaises(OSError):
            self.state.prepare()
        self.assertEqual(list(target.iterdir()), [])

    def test_account_generation_changed_owner_or_hardlink_blocks_reuse(self):
        self.state.prepare()
        stamp = self.state.path / "owner.json"
        original = stamp.read_bytes()
        stamp.write_text("{}\n")
        with self.assertRaises(ServiceError): self.state.verify()
        stamp.write_bytes(original)
        os.link(stamp, self.root / "hardlink")
        with self.assertRaises(ServiceError): self.state.verify()
        (self.root / "hardlink").unlink()
        self.files.accounts.by_uid.return_value = LocalAccount("other", 1002, 1002, "/home/other", "/bin/bash")
        with self.assertRaises(ServiceError): self.state.verify()

    def test_recreated_home_cannot_reuse_opaque_provider_credentials(self):
        self.state.prepare()
        self.home_mock.return_value = (11, 23)
        with self.assertRaises(ServiceError):
            self.state.verify()
        again = NetBirdState(self.files, self.root)
        try:
            with self.assertRaises(ServiceError):
                again.prepare()
        finally:
            again.release_handles()

    def test_directory_replacement_and_socket_replacement_are_not_adopted(self):
        self.state.prepare()
        saved = self.state.path.with_name("saved")
        self.state.path.rename(saved)
        self.state.path.mkdir(mode=0o700)
        (self.state.path / "owner.json").write_bytes((saved / "owner.json").read_bytes())
        with self.assertRaises(ServiceError): self.state.verify()

    def test_provider_socket_requires_private_directory_and_expected_group(self):
        scope = NetBirdScope(Mock(files=self.files))
        scope.directory = self.root / "socket-dir"
        scope.socket = scope.directory / "control.sock"
        scope.directory.mkdir(mode=0o750)
        try:
            os.chown(scope.directory, 0, 1002)
        except PermissionError:
            self.skipTest("CAP_CHOWN is absent from this isolated test container")
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(server.close)
        server.bind(str(scope.socket))
        os.chown(scope.socket, 0, 1002)
        value = scope.socket_node()
        self.assertEqual(value[-1], scope.socket.stat().st_ino)
        scope.directory.chmod(0o755)
        with self.assertRaises(ServiceError): scope.socket_node()
