# SPDX-License-Identifier: GPL-3.0-or-later
from copy import deepcopy
from dataclasses import asdict, replace
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.journal import JournalRetained, validate_record
from karyo_profiles.journal_netbird import JournalNetBird
from karyo_profiles.journal_netbird_record import validate_provider
from karyo_profiles.networks import NetworkSpec
from test_journal import JournalFixture, WORK, SOURCE
from test_journal_dns import configuration, proof

SPEC = NetworkSpec(WORK.uid, "ab" * 16)


def provider():
    return {"kind": "netbird", "pending": None,
            "state": {"directory": [2, 3], "stamp": [2, 4], "home": [2, 5]},
            "service": {"unit": dict(proof(), namespace=[4, 100], runtime=[[5, 10]]),
                        "socket": [5, 10, 5, 11], "endpoint": [5, 12]}}


def record(token=SPEC.token):
    dns = configuration()
    dns.update(pending=None, files={"directory": [1, 3], "files": {"bus.conf": [1, 4], "resolved.conf": [1, 5]}},
               bus=proof(), resolver=dict(proof(), runtime=[[1, 5], [1, 6]]))
    return {"token": token, "account": asdict(WORK),
            "source": {"id": SOURCE.id, "name": SOURCE.name, "uid": SOURCE.uid, "vt": SOURCE.vt},
            "pending": None, "network": {"namespace": [4, 100], "indices": [9, 2],
                "macs": ["02:00:00:00:00:01", "02:00:00:00:00:02"]}, "dns": dns}


class ProviderSchemaTest(unittest.TestCase):
    def test_only_derived_provider_and_native_identities_are_accepted(self):
        value = provider()
        validate_provider(value)
        validate_record(dict(record(), provider=value))
        for key, data in (("kind", "other"), ("path", "/etc/passwd"), ("pending", True)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_provider(dict(value, **{key: data}))
        for section, key, data in (("state", "home", [True, 2]), ("service", "socket", [5, 20, 5, 11]),
                                   ("service", "endpoint", [0, 12])):
            changed = deepcopy(value); changed[section][key] = data
            with self.subTest(section=section, key=key), self.assertRaises(ValueError): validate_provider(changed)

    def test_creation_and_stop_intents_cannot_claim_missing_or_ambiguous_proof(self):
        validate_provider({"kind": "netbird", "pending": "state-prepare", "state": None, "service": None})
        value = provider()
        validate_provider(dict(value, pending="service-start", service=None))
        validate_provider(dict(value, pending="service-stop"))
        for changed in (dict(value, pending="state-prepare"), dict(value, pending="service-start"),
                        dict(value, state=None), dict(value, service=None), dict(value, pending="service-stop", service=None)):
            with self.assertRaises(ValueError): validate_provider(changed)

    def test_provider_requires_acknowledged_dns_and_same_namespace(self):
        value = dict(record(), provider=provider())
        changed = deepcopy(value); changed["dns"]["pending"] = "resolver-stop"
        with self.assertRaises(ValueError): validate_record(changed)
        changed = deepcopy(value); changed["provider"]["service"]["unit"]["namespace"] = [4, 999]
        with self.assertRaises(ValueError): validate_record(changed)
        changed = deepcopy(value); changed.pop("dns")
        with self.assertRaises(ValueError): validate_record(changed)


class Memory:
    def __init__(self): self.value = record()
    def record(self, token): return deepcopy(self.value)
    def change(self, before, after):
        assert before == self.value
        validate_record(after)
        self.value = deepcopy(after)


class ProviderLifecycleTest(unittest.TestCase):
    def setUp(self):
        self.journal = Memory()
        self.dns = Mock(token=SPEC.token)
        self.dns.files.network.spec = SPEC
        self.dns.files.accounts.by_uid.return_value = WORK
        self.scope = Mock()
        self.scope.unit.closed = False
        self.scope.unit.process.exited.return_value = False
        self.scope.unit.cgroup_pids.return_value = set()
        self.scope.unit.name = "karyo-netbird-" + SPEC.token + ".service"
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.scope.directory = Path(temporary.name) / 'provider-runtime'

    def test_write_ahead_order_precedes_each_native_creation(self):
        events = []
        def state_prepare():
            self.assertEqual(self.journal.value["provider"]["pending"], "state-prepare")
            self.assertEqual(self.journal.value["network"]["contents"], "tunnel")
            self.assertEqual(self.scope.network.contents, "tunnel")
            events.append("state")
        def start():
            self.assertEqual(self.journal.value["provider"]["pending"], "service-start")
            events.append("service")
        self.scope.state.prepare.side_effect = state_prepare
        self.scope.start_prepared.side_effect = start
        with patch("karyo_profiles.journal_netbird.NetBirdScope", return_value=self.scope), \
                patch("karyo_profiles.journal_netbird.state_receipt", return_value=provider()["state"]), \
                patch("karyo_profiles.journal_netbird.service_receipt", return_value=provider()["service"]):
            owner = JournalNetBird.start(self.journal, self.dns)
            owner.verify()
        self.assertEqual(events, ["state", "service"])
        self.assertEqual(self.journal.value["provider"], provider())
        self.scope.release_handles.assert_not_called()

    def test_lost_creation_ack_keeps_intent_and_releases_only_observers(self):
        self.scope.start_prepared.side_effect = TimeoutError("unacknowledged")
        with patch("karyo_profiles.journal_netbird.NetBirdScope", return_value=self.scope), \
                patch("karyo_profiles.journal_netbird.state_receipt", return_value=provider()["state"]):
            with self.assertRaises(TimeoutError): JournalNetBird.start(self.journal, self.dns)
        self.assertEqual(self.journal.value["provider"]["pending"], "service-start")
        self.assertIsNone(self.journal.value["provider"]["service"])
        self.scope.release_handles.assert_called_once_with()
        self.scope.close.assert_not_called()
        with self.assertRaises(JournalRetained): JournalNetBird.restore(self.journal, self.dns)

    def test_stop_intent_is_written_before_native_stop_and_only_provider_field_is_removed(self):
        self.journal.value["provider"] = provider()
        def stop(): self.assertEqual(self.journal.value["provider"]["pending"], "service-stop")
        self.scope.close.side_effect = stop
        with patch("karyo_profiles.journal_netbird.state_receipt", return_value=provider()["state"]), \
                patch("karyo_profiles.journal_netbird.service_receipt", return_value=provider()["service"]):
            JournalNetBird(self.journal, self.dns, self.scope).close()
        self.assertEqual(self.journal.value, record())
        self.scope.state.prepare.assert_not_called()

    def test_lost_stop_ack_remains_pending_and_changed_account_is_refused(self):
        self.journal.value["provider"] = provider()
        self.scope.close.side_effect = TimeoutError("stop uncertain")
        with patch("karyo_profiles.journal_netbird.state_receipt", return_value=provider()["state"]), \
                patch("karyo_profiles.journal_netbird.service_receipt", return_value=provider()["service"]):
            with self.assertRaises(TimeoutError): JournalNetBird(self.journal, self.dns, self.scope).close()
        self.assertEqual(self.journal.value["provider"]["pending"], "service-stop")
        self.dns.files.accounts.by_uid.return_value = replace(WORK, home="/home/replaced")
        with self.assertRaises(JournalRetained): JournalNetBird.restore(self.journal, self.dns)


    def test_restore_keeps_runtime_objects_after_receipt_mismatch(self):
        self.journal.value["provider"] = provider()
        self.scope.unit.process.open.return_value = 99
        with patch("karyo_profiles.journal_netbird.NetBirdScope", return_value=self.scope), \
                patch("karyo_profiles.journal_netbird.restore_state"), \
                patch('karyo_profiles.provider_worker.process_gone', return_value=False), \
                patch("karyo_profiles.journal_netbird.service_receipt", return_value=dict(provider()["service"], endpoint=[5, 99])):
            with self.assertRaises(JournalRetained): JournalNetBird.restore(self.journal, self.dns)
        self.scope.release_handles.assert_called_once_with()
        self.scope.close.assert_not_called()
        self.assertEqual(self.journal.value["provider"], provider())

    def test_lost_stop_ack_needs_dead_process_and_absent_runtime(self):
        self.journal.value["provider"] = dict(provider(), pending="service-stop")
        self.scope.unit.info.return_value = {"LoadState": "not-found"}
        self.scope.unit.closed = False
        with patch("karyo_profiles.journal_netbird.NetBirdScope", return_value=self.scope), \
                patch("karyo_profiles.journal_netbird.restore_state"), \
                patch('karyo_profiles.provider_worker.process_gone', return_value=True):
            restored = JournalNetBird.restore(self.journal, self.dns)
            self.assertTrue(restored.scope.unit.closed)
            self.scope.unit.process.open.assert_not_called()
            self.scope.directory.mkdir()
            with self.assertRaises(JournalRetained): JournalNetBird.restore(self.journal, self.dns)
        self.scope.close.assert_not_called()


@unittest.skipUnless(os.geteuid() == 0, "root-owned native journal files")
class ProviderJournalTest(JournalFixture, unittest.TestCase):
    def add_profile(self):
        token = self.journal.begin(WORK, SOURCE)
        self.journal.change(self.journal.record(token), record(token))
        return token

    def test_schema5_roundtrip_keeps_older_network_dns_and_forwarding_records(self):
        token = self.add_profile()
        before = self.journal.record(token)
        self.journal.change(before, dict(before, provider=provider()))
        self.assertEqual(self.journal.snapshot()["schema"], 5)
        self.reopen()
        self.assertEqual(self.journal.record(token)["provider"], provider())
        before = self.journal.record(token); after = dict(before); after.pop("provider")
        self.journal.change(before, after)
        self.assertEqual(self.journal.snapshot()["schema"], 5)
        self.reopen()
        self.assertEqual(self.journal.record(token), record(token))

    def test_pending_provider_blocks_all_new_profile_allocation(self):
        token = self.add_profile(); before = self.journal.record(token)
        self.journal.change(before, dict(before, provider=dict(provider(), pending="service-start", service=None)))
        raw = self.journal.raw
        with self.assertRaises(JournalRetained): self.journal.begin(replace(WORK, name="other", uid=1003), SOURCE)
        self.assertEqual(self.journal.raw, raw)
