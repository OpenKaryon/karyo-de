# SPDX-License-Identifier: GPL-3.0-or-later
from copy import deepcopy
from dataclasses import asdict,replace
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.forwarding import ForwardingRetained
from karyo_profiles.journal import JournalRetained,validate_record
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.journal_forwarding_record import validate_forwarding
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.networks import NetworkSpec
import test_forwarding as forwarding_fixture
import test_journal as journal_fixture

PHYSICAL=forwarding_fixture.PHYSICAL


def base_record(spec):
    return {"token":spec.token,"account":asdict(journal_fixture.WORK),
        "source":{"id":"c1","name":"home","uid":1001,"vt":8},"pending":None,
        "network":{"namespace":[4,100],"indices":[9,2],"macs":["02:00:00:00:00:01","02:00:00:00:00:02"]}}


class Journal:
    def __init__(self,record):self.current=deepcopy(record);self.fail_at=None;self.acks=0
    def record(self,token):
        if self.current["token"]!=token:raise JournalRetained("Wrong token")
        return deepcopy(self.current)
    def change(self,before,after):
        if before!=self.current:raise JournalRetained("Record changed")
        validate_record(after)
        if ("forwarding" in before and "forwarding" in after and before["forwarding"]["pending"]=="remove"
                and before["forwarding"]["proof"]!=after["forwarding"]["proof"]):
            self.acks+=1
            if self.acks==self.fail_at:raise RuntimeError("Controller interrupted before journal ACK")
        self.current=deepcopy(after)


class GatewayFixture:
    def setUp(self):
        original=Path.read_text
        def read(path,*args,**kwargs):
            return "1\n" if str(path)=="/proc/sys/net/ipv4/ip_forward" else original(path,*args,**kwargs)
        for item in (patch("karyo_profiles.forwarding.discover_underlay",return_value=PHYSICAL),
                     patch("karyo_profiles.forwarding.Path.read_text",new=read)):
            item.start();self.addCleanup(item.stop)
    def network(self,spec):
        self.kernel=forwarding_fixture.Kernel(spec)
        value=SimpleNamespace(spec=spec,command=self.kernel,verify=Mock(),_empty=Mock())
        return value
    def fresh(self):
        spec=NetworkSpec(1002,"ab"*16);journal=Journal(base_record(spec))
        wrapped=JournalNetwork(journal,self.network(spec))
        return journal,wrapped
    def start(self):
        journal,wrapped=self.fresh();scope=JournalForwarding.start(journal,wrapped,PHYSICAL)
        return journal,wrapped,scope
    def deletes(self):return [args for args,data in self.kernel.calls if "delete" in args]


class ForwardingJournalTest(GatewayFixture,unittest.TestCase):
    def test_complete_receipt_restores_native_lease_without_recreation_or_empty_consumer_assumption(self):
        journal,wrapped,scope=self.start();before=deepcopy(journal.current)
        mutation_count=len([args for args,data in self.kernel.calls if "add" in args or "-f" in args])
        wrapped.network._empty.side_effect=RuntimeError("Living consumers")
        restored=JournalForwarding.restore(journal,wrapped);restored.verify()
        self.assertEqual(journal.current,before)
        self.assertEqual(len([args for args,data in self.kernel.calls if "add" in args or "-f" in args]),mutation_count)
        wrapped.network._empty.side_effect=None;restored.close()
        self.assertNotIn("forwarding",journal.current);self.assertFalse(self.kernel.routes);self.assertIsNone(self.kernel.firewall)

    def test_every_native_delete_sees_close_intent_and_captures_its_ack(self):
        journal,wrapped,scope=self.start();run=self.kernel.run;observed=[]
        def native(*args,**kwargs):
            if "delete" in args:
                self.assertEqual(journal.current["forwarding"]["pending"],"remove")
                observed.append(deepcopy(journal.current["forwarding"]["proof"]))
            return run(*args,**kwargs)
        self.kernel.run=native;scope.close()
        self.assertEqual(len(observed),7);self.assertEqual(journal.acks,7)
        self.assertEqual([len(row["rules"]) for row in observed[:3]],[2,1,0])
        self.assertEqual([len(row["routes"]) for row in observed[3:]],[4,3,2,1])

    def test_each_single_interrupted_native_delete_reconciles_only_the_next_fixed_step(self):
        for step in range(1,8):
            with self.subTest(step=step):
                journal,wrapped,scope=self.start();journal.fail_at=step
                with self.assertRaises(ForwardingRetained):scope.close()
                journal.fail_at=None
                restored=JournalForwarding.restore(journal,wrapped);restored.close()
                self.assertNotIn("forwarding",journal.current)
                self.assertFalse(self.kernel.routes);self.assertIsNone(self.kernel.firewall)
                self.assertEqual(len(self.deletes()),7)

    def test_unacknowledged_creation_is_retained_without_native_mutation(self):
        journal,wrapped=self.fresh();value={"physical":asdict(PHYSICAL),"pending":"create","proof":None}
        journal.change(journal.current,dict(journal.current,forwarding=value))
        with self.assertRaises(JournalRetained):JournalForwarding.restore(journal,wrapped)
        self.assertFalse(self.deletes());self.assertFalse(self.kernel.routes)

    def test_changed_live_route_or_rule_is_never_removed(self):
        journal,wrapped,scope=self.start();self.kernel.routes[-1]["metric"]=333
        with self.assertRaises(RuntimeError):JournalForwarding.restore(journal,wrapped)
        self.assertFalse(self.deletes())
        self.assertIn("forwarding",journal.current)

    def test_replaced_firewall_after_recorded_delete_is_not_adopted_by_name(self):
        journal,wrapped,scope=self.start();journal.fail_at=3
        with self.assertRaises(ForwardingRetained):scope.close()
        journal.fail_at=None
        spec=wrapped.network.spec
        self.kernel.firewall=[{"table":{"family":"inet","name":"karyo_fwd_"+spec.token,"comment":spec.alias,"handle":999}}]
        before=len(self.deletes())
        with self.assertRaises(RuntimeError):JournalForwarding.restore(journal,wrapped)
        self.assertEqual(len(self.deletes()),before);self.assertEqual(self.kernel.firewall[0]["table"]["handle"],999)

    def test_non_suffix_route_loss_is_not_mistaken_for_interrupted_close(self):
        journal,wrapped,scope=self.start();journal.fail_at=4
        with self.assertRaises(ForwardingRetained):scope.close()
        # Reconstruct the recorded phase, but lose the sentinel instead of the
        # next fixed physical default. No deletion command may follow.
        proof=journal.current["forwarding"]["proof"]
        self.kernel.routes=[]
        for args,row in zip(scope.gateway.route_plan(),proof["routes"]):
            self.kernel.run("/usr/sbin/ip","-4","route","add",*args[0],"table",str(scope.gateway.table),"proto","static")
        self.kernel.routes.pop(0);journal.fail_at=None;before=len(self.deletes())
        with self.assertRaises(RuntimeError):JournalForwarding.restore(journal,wrapped)
        self.assertEqual(len(self.deletes()),before)

    def test_schema_rejects_commands_foreign_selectors_and_invalid_dependency_order(self):
        journal,wrapped,scope=self.start();original=journal.current["forwarding"]
        cases=[]
        value=deepcopy(original);value["proof"]["argv"]=["/bin/sh"];cases.append(value)
        value=deepcopy(original);value["proof"]["routes"][0]["dst"]="192.0.2.99/32";cases.append(value)
        value=deepcopy(original);value["proof"]["rules"][0]["src"]="192.0.2.99";cases.append(value)
        value=deepcopy(original);value["proof"]["priorities"][0]=True;cases.append(value)
        value=deepcopy(original);value["proof"]["firewall"]["snapshot"][0]["table"]["name"]="foreign";cases.append(value)
        value=deepcopy(original);value["proof"]["firewall"]=None;value["pending"]="remove";cases.append(value)
        for value in cases:
            with self.subTest(value=value),self.assertRaises((ValueError,RuntimeError)):
                validate_forwarding(value,wrapped.network.spec)

    def test_dns_seed_matches_acknowledged_forwarding_and_cannot_overlap_its_mutation(self):
        journal,wrapped,scope=self.start();record=deepcopy(journal.current)
        record["dns"]={"seed":{"physical":asdict(PHYSICAL),"servers":[PHYSICAL.gateway],"search":[]},
            "service_uid":990,"gid":1002,"pending":"files-create","files":None,"bus":None,"resolver":None}
        validate_record(record)
        changed=deepcopy(record);changed["forwarding"]["pending"]="remove"
        with self.assertRaises(ValueError):validate_record(changed)
        changed=deepcopy(record);changed["dns"]["seed"]["physical"]["metric"]+=1
        with self.assertRaises(ValueError):validate_record(changed)

    def test_dns_dependency_blocks_forwarding_deletion_before_native_calls(self):
        journal,wrapped,scope=self.start()
        journal.current["dns"]={"synthetic":"only guard is used"}
        with self.assertRaises(JournalRetained):scope.close()
        self.assertFalse(self.deletes())


@unittest.skipUnless(os.geteuid()==0,"real private schema4 journal files")
class HeaderTest(journal_fixture.JournalFixture,GatewayFixture,unittest.TestCase):
    def setUp(self):
        journal_fixture.JournalFixture.setUp(self);GatewayFixture.setUp(self)
        token=self.journal.begin(journal_fixture.WORK,journal_fixture.SOURCE)
        before=self.journal.record(token);captured=base_record(NetworkSpec(1002,token))
        self.journal.change(before,captured)
        self.wrapped=JournalNetwork(self.journal,self.network(NetworkSpec(1002,token)))
    def test_schema4_roundtrip_keeps_receipts_and_private_file_permissions(self):
        JournalForwarding.start(self.journal,self.wrapped,PHYSICAL)
        self.assertEqual(self.journal.snapshot()["schema"],4)
        self.reopen();wrapped=JournalNetwork(self.journal,self.wrapped.network)
        JournalForwarding.restore(self.journal,wrapped).close()
        self.assertEqual(self.journal.snapshot()["schema"],4)
        self.assertNotIn("forwarding",self.journal.record(wrapped.token))
        self.assertEqual((self.path/"state.json").stat().st_mode&0o777,0o600)
    def test_current_forwarding_close_intent_blocks_new_allocation(self):
        JournalForwarding.start(self.journal,self.wrapped,PHYSICAL)
        before=self.journal.record(self.wrapped.token)
        self.journal.change(before,dict(before,forwarding=dict(before["forwarding"],pending="remove")))
        with self.assertRaises(JournalRetained):self.journal.begin(replace(journal_fixture.WORK,name="experiments",uid=1003),journal_fixture.SOURCE)


if __name__=="__main__":unittest.main()
