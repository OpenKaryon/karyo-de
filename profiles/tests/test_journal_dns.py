# SPDX-License-Identifier: GPL-3.0-or-later
from dataclasses import asdict
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.dns import ResolverFiles
from karyo_profiles.journal import JournalRetained,validate_record
from karyo_profiles.journal_dns import file_receipt,restore_files,restore_unit,runtime_identity
from karyo_profiles.journal_dns_record import validate_dns
from karyo_profiles.networks import NetworkSpec
from karyo_profiles.services import OwnedUnit
from karyo_profiles.storage import ConfigStore
from test_dns import SEED
from test_services import Kernel

SPEC=NetworkSpec(1002,"ab"*16)


def configuration():
    return {"seed":json.loads(json.dumps(asdict(SEED))),"gid":1002,"service_uid":990,
            "pending":"files-create","files":None,"bus":None,"resolver":None}


def proof():
    return {"invocation":"bc"*16,"pid":345,"start":456,"exe":[1,2],"namespace":[1,2],"runtime":[[1,5]]}


class SchemaTest(unittest.TestCase):
    def test_fixed_seed_and_no_caller_selected_paths_or_programs(self):
        value=configuration();validate_dns(value,SPEC,1002)
        for key in ("path","program","unit","owner"):
            changed=dict(value,**{key:"/etc/passwd"})
            with self.assertRaises(ValueError):validate_dns(changed,SPEC,1002)
        value["seed"]["physical"]["interface"]="../eth0"
        with self.assertRaises(RuntimeError):validate_dns(value,SPEC,1002)

    def test_file_names_identity_types_and_unit_dependencies_are_strict(self):
        value=configuration();value.update(pending=None,files={"directory":[2,3],"files":{"bus.conf":[2,4],"resolved.conf":[2,5]}})
        validate_dns(value,SPEC,1002)
        for changed in (dict(value,resolver=dict(proof(),runtime=[[1,5],[1,6]])),
                        dict(value,pending="bus-stop"),dict(value,pending="resolver-start",resolver=proof())):
            with self.assertRaises(ValueError):validate_dns(changed,SPEC,1002)
        value["files"]["files"]["../foreign"]=[2,6]
        with self.assertRaises(ValueError):validate_dns(value,SPEC,1002)

    def test_unit_pid_start_invocation_and_runtime_receipts_reject_ambiguity(self):
        value=configuration();value.update(pending=None,files={"directory":[2,3],"files":{"bus.conf":[2,4],"resolved.conf":[2,5]}},bus=proof())
        for key,wrong in (("pid",True),("start",0),("invocation","other"),("runtime",[]),("exe",[True,2])):
            changed=dict(value,bus=dict(proof(),**{key:wrong}))
            with self.assertRaises(ValueError):validate_dns(changed,SPEC,1002)


@unittest.skipUnless(os.geteuid()==0,"real captured private resolver files")
class FilesTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir="/root");self.addCleanup(self.temp.cleanup)
        self.store=ConfigStore(runtime=Path(self.temp.name)/"runtime")
        held=self.store.locked();held.__enter__();self.addCleanup(held.__exit__,None,None,None)
        self.network=SimpleNamespace(spec=SPEC,store=self.store,verify=lambda:None,_empty=lambda:None)
        self.accounts=SimpleNamespace(by_uid=lambda uid:SimpleNamespace(uid=uid,gid=1002))
        for item in (patch("karyo_profiles.dns.supported_resolver_layout",return_value="stub-resolv.conf"),
                     patch("karyo_profiles.dns.resolver_service_uid",return_value=990)):
            item.start();self.addCleanup(item.stop)
        self.files=ResolverFiles(self.network,SEED,self.accounts).create()
        self.dns=configuration();self.dns.update(pending=None,files=file_receipt(self.files))
        self.addCleanup(self.release)

    def release(self):
        if self.files.fd is not None:os.close(self.files.fd);self.files.fd=None

    def restored(self):
        files=restore_files(self.network,self.dns,self.accounts)
        self.addCleanup(lambda:os.close(files.fd) if files.fd is not None else None)
        return files

    def test_recapture_same_directory_and_files_keeps_original_inode_and_bytes(self):
        before={p.name:(p.stat().st_ino,p.read_bytes()) for p in Path(self.files.plan.config).iterdir()}
        restored=self.restored()
        self.assertEqual(file_receipt(restored),self.dns["files"])
        self.assertEqual({p.name:(p.stat().st_ino,p.read_bytes()) for p in Path(self.files.plan.config).iterdir()},before)

    def test_network_service_view_recaptures_its_own_versioned_template(self):
        network, accounts = self.files.network, self.files.accounts
        self.files.close()
        self.files = ResolverFiles(network, SEED, accounts, service_view='network-manager').create()
        self.dns.update(view='network-manager', files=file_receipt(self.files))
        validate_dns(self.dns, SPEC, 1002)
        restored = self.restored()
        self.assertEqual(restored.plan.service_view, 'network-manager')
        self.assertEqual(restored.plan.files(), self.files.plan.files())
        self.assertEqual(file_receipt(restored), self.dns['files'])

    def test_changed_content_or_replaced_file_is_retained(self):
        target=Path(self.files.plan.config)/"bus.conf";original=target.read_bytes()
        target.write_bytes(original+b"changed")
        with self.assertRaises(RuntimeError):self.restored()
        target.write_bytes(original)
        replacement=target.parent/"replacement";replacement.write_bytes(original);replacement.chmod(0o644);replacement.replace(target)
        with self.assertRaises(RuntimeError):self.restored()
        self.assertTrue(target.exists())

    def test_partial_removal_accepts_only_remaining_known_files(self):
        target=Path(self.files.plan.config)/"bus.conf";target.unlink()
        with self.assertRaises(RuntimeError):self.restored()
        self.dns["pending"]="files-remove"
        files=self.restored();self.assertEqual(set(files.captured),{"resolved.conf"})
        files.close();self.assertFalse(Path(self.files.plan.config).exists())

    def test_directory_already_removed_is_accepted_only_under_removal_intent(self):
        self.files.close()
        with self.assertRaises(RuntimeError):self.restored()
        self.dns["pending"]="files-remove";files=self.restored()
        self.assertTrue(files.closed);self.assertIsNone(files.fd)

    def test_runtime_directory_requires_expected_owner_group_mode_and_inode(self):
        path=Path(self.temp.name)/"runtime-dir";path.mkdir(mode=0o750)
        expected=runtime_identity(str(path),0,os.getegid())
        self.assertEqual(expected,[path.stat().st_dev,path.stat().st_ino])
        with self.assertRaises(JournalRetained):runtime_identity(str(path),0,1002)
        path.chmod(0o770)
        with self.assertRaises(JournalRetained):runtime_identity(str(path),0,os.getegid())


class UnitRestoreTest(unittest.TestCase):
    def setUp(self):
        self.kernel=Kernel();self.store=SimpleNamespace(_locked=True)
        self.files=SimpleNamespace(network=SimpleNamespace(identity=(1,2)),plan=SimpleNamespace(gid=1002,
            runtime_paths=("/run/fixed-resolver","/run/fixed-bus","/run/fixed-netif")))
        self.unit=self.make()
        with patch.object(self.unit,"capture_executable",return_value=(1,2)):
            self.unit.start(("User=root","Group=1002"))
        self.kernel.calls.clear()
        for item in (patch("karyo_profiles.journal_dns.os.stat",return_value=SimpleNamespace(st_dev=1,st_ino=2)),
                     patch("karyo_profiles.journal_dns.runtime_identity",return_value=[1,5])):
            item.start();self.addCleanup(item.stop)

    def make(self):
        return OwnedUnit(self.store,SPEC,"dns-bus",0,self.kernel,process=self.kernel,host_guard=lambda:None)
    def mutations(self):return [c for c in self.kernel.calls if c[0].endswith("systemd-run") or "stop" in c]

    def test_live_unit_is_recaptured_without_restart_or_stop(self):
        restored=self.make();restore_unit(self.files,"bus",restored,proof(),False)
        self.assertEqual(restored.identity.pid,345);self.assertEqual(restored.pidfd,123)
        self.assertEqual(self.mutations(),[])
        restored.process.close(restored.pidfd);restored.pidfd=None

    def test_replaced_invocation_pid_cgroup_or_start_is_never_stopped(self):
        original=dict(self.kernel.rows)
        for field,value in (("InvocationID","de"*16),("MainPID","999"),("ControlGroup","/foreign")):
            self.kernel.rows[field]=value
            with self.assertRaises(RuntimeError):restore_unit(self.files,"bus",self.make(),proof(),False)
            self.kernel.rows=dict(original)
        self.kernel.start_time=999
        with self.assertRaises(RuntimeError):restore_unit(self.files,"bus",self.make(),proof(),False)
        self.assertEqual(self.mutations(),[])

    def test_changed_runtime_inode_is_retained_without_service_stop(self):
        with patch("karyo_profiles.journal_dns.runtime_identity",return_value=[1,6]):
            with self.assertRaises(JournalRetained):restore_unit(self.files,"bus",self.make(),proof(),False)
        self.assertEqual(self.mutations(),[])

    def test_completed_stop_needs_pending_intent_process_exit_and_no_runtime(self):
        self.kernel.rows.update(LoadState="not-found",MainPID="0",ActiveState="inactive");self.kernel.alive=False
        with patch("karyo_profiles.journal_dns.process_gone",return_value=True),patch("karyo_profiles.journal_dns.os.path.lexists",return_value=False):
            restored=self.make();restore_unit(self.files,"bus",restored,proof(),True)
            self.assertTrue(restored.closed);self.assertIsNone(restored.pidfd)
            with self.assertRaises(RuntimeError):restore_unit(self.files,"bus",self.make(),proof(),False)
        self.assertEqual(self.mutations(),[])

    def test_unrecorded_service_is_never_adopted(self):
        with self.assertRaises(JournalRetained):restore_unit(self.files,"bus",self.make(),None,False)
        self.assertEqual(self.mutations(),[])


if __name__=="__main__":unittest.main()
