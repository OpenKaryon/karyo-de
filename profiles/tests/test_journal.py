# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
from dataclasses import replace
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.journal import JournalRetained,RuntimeJournal
from karyo_profiles.journal_network import JournalNetwork,recover_networks
from karyo_profiles.logind import Session
from karyo_profiles.model import Ownership
from test_networks import Kernel,Lease

HOME=LocalAccount("home",1001,1001,"/home/home","/bin/bash")
WORK=LocalAccount("work",1002,1002,"/home/work","/bin/bash")
SOURCE=Session("c1","home",1001,8,True)


class JournalFixture:
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir="/root")
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.path=self.root/"run/profile-host"
        self.journal=RuntimeJournal(self.path)
        self.addCleanup(self.journal.close)

    def reopen(self):
        self.journal.close();self.journal=RuntimeJournal(self.path);self.addCleanup(self.journal.close)
        return self.journal


@unittest.skipUnless(os.geteuid()==0,"root-owned host resource journal")
class JournalTest(JournalFixture,unittest.TestCase):
    def test_private_modes_cloexec_and_empty_reopen(self):
        self.assertEqual(self.path.stat().st_mode&0o777,0o700)
        for name in ("controller.lock","state.json"):
            self.assertEqual((self.path/name).stat().st_mode&0o777,0o600)
        self.assertFalse(os.get_inheritable(self.journal.directory));self.assertFalse(os.get_inheritable(self.journal.lock))
        self.assertEqual(self.reopen().snapshot()["profiles"],[])

    def test_parallel_process_cannot_share_controller_lock(self):
        code="""import sys
sys.path.insert(0,sys.argv[1])
from karyo_profiles.journal import RuntimeJournal,JournalRetained
try:RuntimeJournal(sys.argv[2])
except JournalRetained:raise SystemExit(0)
raise SystemExit(2)
"""
        result=subprocess.run([sys.executable,"-I","-c",code,str(Path(__file__).resolve().parents[1]),str(self.path)],capture_output=True,timeout=5)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_new_intent_precedes_capture_and_multiple_current_profiles_can_coexist(self):
        first=self.journal.begin(WORK,SOURCE)
        second=self.journal.begin(replace(WORK,name="experiments",uid=1003),SOURCE)
        self.assertNotEqual(first,second)
        self.assertEqual(len(self.journal.snapshot()["profiles"]),2)
        self.assertEqual(self.journal.record(first)["pending"],"create")
        self.assertIsNone(self.journal.record(first)["network"])
        before=(self.path/"state.json").read_bytes()
        self.reopen()
        with self.assertRaises(JournalRetained):self.journal.begin(replace(WORK,name="third",uid=1004),SOURCE)
        self.assertEqual((self.path/"state.json").read_bytes(),before)

    def test_returned_snapshots_cannot_change_live_journal(self):
        token=self.journal.begin(WORK,SOURCE)
        result=self.journal.snapshot();result["profiles"].clear()
        self.assertEqual(self.journal.record(token)["account"]["name"],"work")

    def test_duplicate_account_and_unknown_or_invalid_records_are_not_published(self):
        self.journal.begin(WORK,SOURCE);before=(self.path/"state.json").read_bytes()
        with self.assertRaises(ValueError):self.journal.begin(WORK,SOURCE)
        for field,value in (("pending",[]),("network",{}),("token","../unknown")):
            data=self.journal.snapshot();data["profiles"][0][field]=value
            with self.assertRaises(ValueError):self.journal.save(data)
        self.assertEqual((self.path/"state.json").read_bytes(),before)

    def test_foreign_boot_unknown_schema_and_duplicate_json_fields_are_retained(self):
        original=(self.path/"state.json").read_bytes();self.journal.close()
        values=[original.replace(b'"schema":1',b'"schema":2'),original.replace(b'"schema":1',b'"schema":1,"schema":1')]
        data=json.loads(original);data["boot"]="00000000-0000-0000-0000-000000000000";values.append(json.dumps(data).encode())
        for raw in values:
            (self.path/"state.json").write_bytes(raw)
            with self.assertRaises((JournalRetained,ValueError)):RuntimeJournal(self.path)
            self.assertEqual((self.path/"state.json").read_bytes(),raw)

    def test_changed_bytes_or_replaced_state_inode_is_not_overwritten(self):
        before=(self.path/"state.json").read_bytes()
        replacement=self.path/"replacement";replacement.write_bytes(before);replacement.chmod(0o600)
        replacement.replace(self.path/"state.json")
        with self.assertRaises(JournalRetained):self.journal.begin(WORK,SOURCE)
        self.assertEqual((self.path/"state.json").read_bytes(),before)

    def test_same_inode_state_edit_and_replaced_lock_on_reopen_are_retained(self):
        state=self.path/"state.json";before=state.read_bytes()
        state.write_bytes(before+b" ")
        with self.assertRaises(JournalRetained):self.journal.snapshot()
        state.write_bytes(before)
        self.journal.close()
        lock=self.path/"controller.lock";lock.rename(self.root/"old-controller-lock")
        lock.touch(mode=0o600)
        with self.assertRaises(JournalRetained):RuntimeJournal(self.path)
        self.assertEqual(state.read_bytes(),before)

    def test_symlink_fifo_hardlink_and_public_state_are_rejected(self):
        self.journal.close();state=self.path/"state.json";original=state.read_bytes();state.unlink()
        target=self.root/"target";target.write_bytes(original);target.chmod(0o600)
        state.symlink_to(target)
        with self.assertRaises(OSError):RuntimeJournal(self.path)
        state.unlink();os.mkfifo(state,0o600)
        with self.assertRaises(PermissionError):RuntimeJournal(self.path)
        state.unlink();os.link(target,state)
        with self.assertRaises(PermissionError):RuntimeJournal(self.path)
        state.unlink();state.write_bytes(original);state.chmod(0o644)
        with self.assertRaises(PermissionError):RuntimeJournal(self.path)
        self.assertEqual(target.read_bytes(),original)

    def test_unknown_objects_replaced_lock_and_forked_owner_are_denied(self):
        unknown=self.path/"unexpected";unknown.touch()
        with self.assertRaises(JournalRetained):self.journal.snapshot()
        unknown.unlink()
        with patch("karyo_profiles.journal.os.getpid",return_value=os.getpid()+1):
            with self.assertRaises(PermissionError):self.journal.snapshot()
        lock=self.path/"controller.lock";lock.rename(self.root/"old-lock")
        lock.touch(mode=0o600)
        with self.assertRaises(JournalRetained):self.journal.snapshot()

    def test_failed_atomic_publication_preserves_prior_intent_and_no_temp_file(self):
        before=(self.path/"state.json").read_bytes()
        with patch("karyo_profiles.storage.os.replace",side_effect=OSError("native rename failed")):
            with self.assertRaises(OSError):self.journal.begin(WORK,SOURCE)
        self.assertEqual((self.path/"state.json").read_bytes(),before)
        self.assertEqual(set(p.name for p in self.path.iterdir()),{"state.json","controller.lock"})


@unittest.skipUnless(os.geteuid()==0,"root-owned journal and network fixtures")
class NetworkJournalTest(JournalFixture,unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.kernels=[];self.leases=[]
        self.store=Mock();self.store._locked=True
        self.store.locked.side_effect=lambda:contextlib.nullcontext(self.store)
        self.store.ownership.return_value=Ownership.parse("home work experiments\n")
        self.accounts=Mock();self.accounts.by_uid.side_effect=lambda uid:{1001:HOME,1002:WORK,1003:replace(WORK,name="experiments",uid=1003)}[uid]
        self.accounts.expiry_day.return_value=None;self.accounts.password_hash.return_value="$y$fixture"
        self.sessions=Mock();self.sessions.get.return_value=SOURCE;self.sessions.owners.return_value=()
        self.patch_uid=patch("karyo_profiles.journal_network.uid_processes",return_value=())
        self.patch_uid.start();self.addCleanup(self.patch_uid.stop)
        self.patch_dir=patch("karyo_profiles.networks.trusted_directory",side_effect=lambda *a,**k:os.open(self.root,os.O_RDONLY|os.O_CLOEXEC))
        self.patch_dir.start();self.addCleanup(self.patch_dir.stop)
        self.current=None
        def factory(store,spec,command=None):
            if self.current is None or self.current.spec!=spec:
                self.current=Kernel(spec);self.kernels.append(self.current)
                old=self.current.run
                def run(*args):
                    if args[1:3] in (("netns","add"),("link","delete")):
                        record=self.journal.record(spec.token)
                        self.assertEqual(record["pending"],"create" if args[1]=="netns" else "remove")
                    result=old(*args)
                    if args[1:3]==("link","add"):
                        self.current.host[spec.interface]["address"]="02:00:00:00:00:01"
                        self.current.inside["eth0"]["address"]="02:00:00:00:00:02"
                    return result
                self.current.run=run
            lease=Lease(store,spec,self.current);self.leases.append(lease);return lease
        self.patch_network=patch("karyo_profiles.journal_network.ProfileNetwork",side_effect=factory)
        self.patch_network.start();self.addCleanup(self.patch_network.stop)
        self.addCleanup(self.release_fds)

    def release_fds(self):
        for lease in self.leases:
            if lease.fd is not None:os.close(lease.fd);lease.fd=None

    def start(self):return JournalNetwork.start(self.journal,self.store,WORK,SOURCE,accounts=self.accounts,sessions=self.sessions)
    def recover(self):recover_networks(self.journal,self.store,accounts=self.accounts,sessions=self.sessions)
    def crash(self):self.release_fds();self.reopen()
    def deletes(self):return [call for call in self.current.calls if "delete" in call]

    def test_write_ahead_create_capture_remove_and_clear(self):
        lease=self.start();record=self.journal.record(lease.token)
        self.assertIsNone(record["pending"]);self.assertEqual(record["network"]["namespace"],[4,100])
        lease.close();self.assertEqual(self.journal.snapshot()["profiles"],[])
        self.assertFalse(self.current.present)

    def test_controller_crash_recovers_only_exact_offline_network(self):
        self.start();self.crash();self.recover()
        self.assertFalse(self.current.present);self.assertEqual(len(self.deletes()),2)
        self.assertEqual(self.journal.snapshot()["profiles"],[])
        self.recover();self.assertEqual(len(self.deletes()),2)

    def test_unacknowledged_creation_never_becomes_a_cleanup_candidate(self):
        self.journal.begin(WORK,SOURCE);self.reopen()
        with self.assertRaises(JournalRetained):self.recover()
        self.assertIsNone(self.current)

    def test_replaced_namespace_changed_mac_or_live_consumer_is_retained(self):
        for kind in ("inode","mac","consumer"):
            with self.subTest(kind=kind):
                # Reset a successful prior case without teaching recovery to
                # accept its unknown replacement.
                if self.current is not None:
                    self.current.identity=(4,100);self.current.pids=""
                    self.current.host[self.current.spec.interface]["address"]="02:00:00:00:00:01"
                    self.recover()
                    self.current=None
                self.start();self.crash()
                if kind=="inode":self.current.identity=(4,101)
                elif kind=="mac":self.current.host[self.current.spec.interface]["address"]="02:00:00:00:00:99"
                else:self.current.pids="54321\n"
                before=len(self.deletes())
                with self.assertRaises(RuntimeError):self.recover()
                self.assertEqual(len(self.deletes()),before)
                self.assertEqual(len(self.journal.snapshot()["profiles"]),1)

    def test_interrupted_remove_after_link_delete_completes_only_namespace(self):
        lease=self.start();record=self.journal.record(lease.token)
        self.journal.change(record,dict(record,pending="remove"))
        self.current.host.clear();self.current.inside.pop("eth0")
        self.crash();self.recover()
        self.assertFalse(self.current.present)
        self.assertEqual(sum("link" in call for call in self.deletes()),0)

    def test_interrupted_remove_after_namespace_delete_only_clears_receipt(self):
        lease=self.start();record=self.journal.record(lease.token)
        self.journal.change(record,dict(record,pending="remove"))
        self.current.host.clear();self.current.inside.pop("eth0");self.current.present=False
        self.crash()
        with patch("karyo_profiles.journal_network.namespace_processes",return_value=False):self.recover()
        self.assertEqual(self.deletes(),[]);self.assertEqual(self.journal.snapshot()["profiles"],[])

    def test_changed_account_or_live_uid_is_not_recovered(self):
        self.start();self.crash()
        self.sessions.owners.return_value=(object(),)
        with self.assertRaises(JournalRetained):self.recover()
        self.assertEqual(self.deletes(),[])

    def test_expired_or_locked_profile_fails_before_journal_intent(self):
        self.accounts.password_hash.return_value="!"
        with self.assertRaises(PermissionError):self.start()
        self.assertEqual(self.journal.snapshot()["profiles"],[]);self.assertIsNone(self.current)


if __name__=="__main__":unittest.main()
