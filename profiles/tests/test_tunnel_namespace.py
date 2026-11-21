# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.journal import validate_network
from karyo_profiles.networks import NetworkError, NetworkRetained
import test_networks as fixtures


def tunnel(kind="wireguard"):
    info = {"info_kind": kind}
    if kind == "tun": info["info_data"] = {"type": "tun", "persist": False}
    return {"ifname": "work-vpn", "ifindex": 3, "link_type": "none", "linkinfo": info}


class TunnelNamespaceTest(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.NetworkTest(); self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.lease, self.kernel = self.fixture.lease, self.fixture.kernel
        self.lease.create()

    def delegate(self, row=None):
        self.lease.contents = "tunnel"
        self.kernel.inside["work-vpn"] = tunnel() if row is None else row

    def test_plain_namespace_does_not_adopt_even_a_virtual_tunnel(self):
        self.kernel.inside["work-vpn"] = tunnel()
        with self.assertRaises(NetworkError): self.lease.verify()
        with self.assertRaises(NetworkRetained): self.lease.close()
        self.assertEqual(self.fixture.deletes(), [])

    def test_delegated_kernel_wireguard_and_userspace_tun_close_the_owned_namespace(self):
        for kind in ("wireguard", "tun"):
            self.delegate(tunnel(kind)); self.lease.verify()
        self.lease.close()
        self.assertFalse(self.kernel.present)
        commands = self.fixture.deletes()
        self.assertEqual(len(commands), 2)
        self.assertFalse(any("work-vpn" in args for args in commands))

    def test_physical_tap_veth_bridge_and_enslaved_or_ambiguous_links_are_retained(self):
        invalid = [dict(tunnel(), link_type="ether"), dict(tunnel(), master="br0"),
                   dict(tunnel(), ifindex=2), tunnel("veth"), tunnel("bridge"), tunnel("dummy"),
                   dict(tunnel("tun"), linkinfo={"info_kind":"tun", "info_data":{"type":"tap"}})]
        for row in invalid:
            self.delegate(row)
            with self.subTest(row=row), self.assertRaises(NetworkRetained): self.lease.close()
            self.assertEqual(self.fixture.deletes(), [])

    def test_second_virtual_device_and_changed_host_pair_are_not_delegated(self):
        self.delegate(); self.kernel.inside["other"] = dict(tunnel(), ifname="other", ifindex=4)
        with self.assertRaises(NetworkRetained): self.lease.close()
        self.kernel.inside.pop("other")
        self.kernel.host[self.lease.spec.interface]["ifalias"] = "foreign"
        with self.assertRaises(NetworkRetained): self.lease.close()
        self.assertEqual(self.fixture.deletes(), [])

    def test_namespace_replacement_or_live_consumer_still_prevents_any_delete(self):
        self.delegate(); original = self.kernel.identity
        self.kernel.identity = (4, 999)
        with self.assertRaises(NetworkRetained): self.lease.close()
        self.kernel.identity = original; self.kernel.pids = "987\n"
        with self.assertRaises(NetworkRetained): self.lease.close()
        self.assertEqual(self.fixture.deletes(), [])


    def test_interrupted_host_pair_removal_reattaches_only_its_delegated_namespace(self):
        from dataclasses import asdict
        from unittest.mock import patch
        from karyo_profiles.journal_network import receipt, restore_network
        from test_journal import WORK
        self.delegate()
        self.kernel.host[self.lease.spec.interface]["address"] = "02:00:00:00:00:01"
        self.kernel.inside["eth0"]["address"] = "02:00:00:00:00:02"
        proof = receipt(self.lease)
        record = {"token":self.lease.spec.token, "account":asdict(WORK), "pending":"remove", "network":proof}
        self.kernel.failure = lambda args: args[1:3] == ("netns", "delete")
        with self.assertRaises(NetworkRetained): self.lease.close()
        self.assertFalse(self.kernel.host)
        self.kernel.failure = None; self.kernel.calls.clear()
        with patch("karyo_profiles.journal_network.ProfileNetwork", side_effect=lambda store,spec,command: fixtures.Lease(store,spec,self.kernel)):
            restored = restore_network(self.lease.store,record)
        try:
            self.assertEqual(restored.contents, "tunnel")
            restored.close()
        finally:
            if restored.fd is not None: os.close(restored.fd)
        self.assertEqual(len(self.fixture.deletes()),1)
        self.assertFalse(self.kernel.present)

    def test_only_fixed_tunnel_policy_is_accepted_in_typed_receipts(self):
        proof = {"namespace":[4,100], "indices":[9,2], "macs":["02:00:00:00:00:01","02:00:00:00:00:02"]}
        validate_network(dict(proof, contents="tunnel"))
        for value in (None, True, "all", "netbird --up"):
            with self.assertRaises(ValueError): validate_network(dict(proof, contents=value))


@unittest.skipUnless(os.geteuid() == 0, "root journal schema transition")
class TunnelJournalTest(unittest.TestCase):
    def test_schema6_preserves_namespace_policy_after_provider_receipt_and_reopen(self):
        from test_journal import JournalFixture, WORK, SOURCE
        from test_journal_netbird import record, provider
        fixture = JournalFixture(); fixture.addCleanup = self.addCleanup; fixture.setUp()
        journal = fixture.journal
        token = journal.begin(WORK, SOURCE); before = journal.record(token)
        value = record(token); value["network"]["contents"] = "tunnel"; value["provider"] = provider()
        journal.change(before, value)
        self.assertEqual(journal.snapshot()["schema"], 6)
        before = journal.record(token); after = dict(before); after.pop("provider")
        journal.change(before, after)
        self.assertEqual(journal.snapshot()["schema"], 6)
        fixture.reopen()
        self.assertEqual(fixture.journal.record(token)["network"]["contents"], "tunnel")
        legacy = fixture.journal.snapshot(); legacy["schema"] = 5
        with self.assertRaises(ValueError): fixture.journal.save(legacy)
