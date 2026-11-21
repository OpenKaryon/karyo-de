# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.logind import LoginOwner, Sessions
from karyo_profiles.services import ServiceError
from karyo_profiles.user_manager import BASE_MANAGER, SCOPED_MANAGER, ManagerPending, ManagerRetained, UserManagerScope, uid_processes, manager_exec_start

ACCOUNT=LocalAccount("work",1002,1002,"/home/work","/bin/bash")
LOGIN=LoginOwner("c1","work",1002,444,"user","session-c1.scope","karyo-desktop-session","online")
MANAGER=LoginOwner("c2","work",1002,345,"manager","","systemd-user","active")


class OwnerCommand:
    def __init__(self,owners):
        self.owners=owners
    def run(self,*args,timeout):
        if args[0]=="list-sessions":
            return json.dumps([{"session":o.id} for o in self.owners])
        owner=next(o for o in self.owners if o.id==args[1])
        rows=dict(Id=owner.id,Name=owner.name,User=str(owner.uid),Leader=str(owner.leader),
                  Class=owner.kind,Scope=owner.scope,Service=owner.service,State=owner.state)
        return "".join(k+"="+v+"\n" for k,v in rows.items())


class LoginOwnersTest(unittest.TestCase):
    def test_inventory_includes_remote_tty_manager_and_closing(self):
        ssh=LoginOwner("c3","work",1002,555,"user","session-c3.scope","sshd","closing")
        home=LoginOwner("c4","home",1001,666,"user","session-c4.scope","login","active")
        owners=Sessions(OwnerCommand((LOGIN,MANAGER,ssh,home))).owners(1002)
        self.assertEqual(owners,(LOGIN,MANAGER,ssh))

    def test_invalid_numeric_scope_duplicate_and_unknown_records_refused(self):
        text=OwnerCommand((LOGIN,)).run("show-session","c1",timeout=1)
        for value in (text+"User=1003\n",text.replace("User=1002","User=01002"),
                      text.replace("session-c1.scope","session-foreign.scope"),
                      text.replace("Leader=444","Leader=-1"),text+"Unknown=yes\n"):
            with self.assertRaises(ValueError):
                LoginOwner.parse(value,"c1")

    def test_owner_identity_ignores_only_lifecycle_state(self):
        values=MANAGER.__dict__|{"state":"closing"}
        self.assertEqual(LoginOwner(**values).identity,MANAGER.identity)
        self.assertNotEqual(LoginOwner(**(values|{"leader":999})).identity,MANAGER.identity)

    def test_inventory_requires_ordinary_target_and_complete_global_deadline(self):
        for uid in (True,"1002",999,65534):
            with self.assertRaises(ValueError):
                Sessions(OwnerCommand((LOGIN,))).owners(uid)
        with self.assertRaises(TimeoutError):
            class Expired(OwnerCommand):
                def run(self,*args,timeout):
                    if timeout<=0:raise TimeoutError("Fixture deadline")
                    return super().run(*args,timeout=timeout)
            Sessions(Expired((LOGIN,)),clock=iter((10,16)).__next__).owners(1002)

    def test_uid_inventory_reads_kernel_credentials(self):
        if not Path("/proc/self/status").exists():self.skipTest("Linux procfs")
        self.assertIn(os.getpid(),uid_processes(os.getuid()))


class Kernel:
    def __init__(self,scope):
        self.scope=scope;self.calls=[];self.alive=True;self.start=456;self.closed=[]
        self.rows=dict(LoadState="loaded",ActiveState="inactive",SubState="dead",MainPID="0",
            InvocationID="",ControlGroup="",DropInPaths="/usr/lib/systemd/system/user@.service.d/native.conf",
            NetworkNamespacePath="")
    def run(self,*args):
        self.calls.append(args)
        if args[1]=="daemon-reload":
            present=(self.scope.directory/self.scope.filename).exists()
            self.rows["DropInPaths"]="/usr/lib/systemd/system/user@.service.d/native.conf"+(
                " "+str(self.scope.directory/self.scope.filename) if present else "")
            self.rows["NetworkNamespacePath"]=self.scope.network.path if present else ""
            return ""
        return "".join(k+"="+v+"\n" for k,v in self.rows.items())
    def activate(self):
        self.rows.update(ActiveState="active",SubState="running",MainPID="345",InvocationID="ab"*16,
            ControlGroup="/user.slice/user-1002.slice/user@1002.service")
    def finish(self):
        self.rows.update(ActiveState="inactive",SubState="dead",MainPID="0",InvocationID="",ControlGroup="")
        self.alive=False
    def open(self,pid):return 123
    def exited(self,fd,seconds=0):return not self.alive
    def read(self,*args):return self.start
    def close(self,fd):self.closed.append(fd)


@unittest.skipUnless(os.geteuid()==0,"root-owned user-manager scope files")
class ManagerScopeTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="karyo-manager-",dir="/root")
        self.addCleanup(self.temp.cleanup)
        network=SimpleNamespace(spec=SimpleNamespace(uid=1002),identity=(4,5),path="/run/netns/karyo-fixture",command=None)
        self.owners=[]
        self.sessions=SimpleNamespace(owners=lambda uid:tuple(self.owners))
        accounts=SimpleNamespace(by_uid=lambda uid:ACCOUNT)
        plan=SimpleNamespace(client_properties=lambda:("NetworkNamespacePath="+network.path,
            "BindReadOnlyPaths=/run/karyo-private:/run/systemd/resolve"),runtime_name="karyo-private")
        files=SimpleNamespace(accounts=accounts,plan=plan,network=network,verify=lambda:None)
        self.scope=UserManagerScope(files,sessions=self.sessions)
        self.scope.directory=Path(self.temp.name)/(self.scope.name+".d")
        self.kernel=Kernel(self.scope)
        self.scope.command=self.scope.process=self.kernel
        original=os.stat
        def native_stat(path,*args,**kwargs):
            if str(path) in {"/run/karyo-private","/proc/345/root/run/systemd/resolve"}:
                return SimpleNamespace(st_dev=4,st_ino=900)
            return original(path,*args,**kwargs)
        patches=(patch("karyo_profiles.user_manager.manager_exec_start",side_effect=lambda *a:
                     SCOPED_MANAGER if (self.scope.directory/self.scope.filename).exists() else BASE_MANAGER),
                 patch("karyo_profiles.user_manager.host_controller"),
                 patch("karyo_profiles.user_manager.uid_processes",return_value=()),
                 patch("karyo_profiles.user_manager.os.path.lexists",return_value=False),
                 patch("karyo_profiles.user_manager.program_identity",return_value=(4,50)),
                 patch("karyo_profiles.user_manager.os.stat",side_effect=native_stat))
        for item in patches:item.start();self.addCleanup(item.stop)
        self.addCleanup(self.release)
    def release(self):
        if self.scope.fd is not None:os.close(self.scope.fd);self.scope.fd=None
    def capture(self):
        self.scope.prepare();self.kernel.activate();self.owners[:]=[LOGIN,MANAGER]
        self.scope.capture(LOGIN)

    def test_fresh_scope_is_removed_only_after_native_manager_finishes(self):
        self.capture();self.scope.verify()
        with self.assertRaises(ManagerPending):self.scope.close()
        self.assertTrue((self.scope.directory/self.scope.filename).exists())
        self.assertFalse(any("stop" in call for call in self.kernel.calls))
        self.kernel.finish();self.owners.clear()
        self.scope.close();self.scope.close()
        self.assertFalse(self.scope.directory.exists())
        self.assertEqual(self.kernel.closed,[123])

    def test_live_or_failed_existing_manager_is_not_reconfigured(self):
        self.kernel.activate()
        with self.assertRaises(ServiceError):self.scope.prepare()
        self.assertFalse(self.scope.directory.exists())
        self.scope.close()
        self.assertFalse(any("daemon-reload" in call for call in self.kernel.calls))

    def test_custom_manager_command_is_refused_before_dropin_creation(self):
        with patch("karyo_profiles.user_manager.manager_exec_start",
                   return_value=("/usr/lib/systemd/systemd",("/usr/lib/systemd/systemd","--user","--custom"),False)):
            with self.assertRaises(ServiceError):self.scope.prepare()
        self.assertFalse(self.scope.directory.exists())

    def test_remote_or_closing_session_prevents_offline_preparation(self):
        self.owners[:]=[LoginOwner("s1","work",1002,555,"user","session-s1.scope","sshd","closing")]
        with self.assertRaises(ServiceError):self.scope.prepare()
        self.assertFalse(self.scope.directory.exists())

    def test_collision_in_scope_directory_is_never_adopted(self):
        self.scope.directory.mkdir();other=self.scope.directory/"foreign"
        other.write_text("keep")
        with self.assertRaises(FileExistsError):self.scope.prepare()
        self.scope.close()
        self.assertEqual(other.read_text(),"keep")

    def test_unknown_file_payload_and_replaced_inode_block_release(self):
        self.scope.prepare();path=self.scope.directory/self.scope.filename
        original=path.read_bytes();path.write_bytes(original+b"changed")
        with self.assertRaises(ManagerRetained):self.scope.close()
        path.write_bytes(original);other=self.scope.directory/"foreign";other.write_text("keep")
        with self.assertRaises(ManagerRetained):self.scope.close()
        other.unlink()
        original_path=self.scope.directory.parent/"old";path.rename(original_path);path.write_bytes(original)
        with self.assertRaises(ManagerRetained):self.scope.close()
        path.unlink();original_path.rename(path)
        self.scope.close()

    def test_stale_invocation_pid_or_process_start_is_retained(self):
        self.capture()
        for key,value in (("InvocationID","cd"*16),("MainPID","999")):
            old=self.kernel.rows[key];self.kernel.rows[key]=value
            with self.assertRaises(ManagerRetained):self.scope.close()
            self.kernel.rows[key]=old
        self.kernel.start+=1
        with self.assertRaises(ManagerRetained):self.scope.close()
        self.assertFalse(any("stop" in call for call in self.kernel.calls))

    def test_unexpected_login_or_user_manager_is_not_captured(self):
        self.scope.prepare();self.kernel.activate()
        ssh=LoginOwner("s1","work",1002,555,"user","session-s1.scope","sshd","online")
        self.owners[:]=[LOGIN,MANAGER,ssh]
        with self.assertRaises(ManagerRetained):self.scope.capture(LOGIN)
        with self.assertRaises(ManagerRetained):self.scope.close()
        self.assertIsNone(self.scope.identity)

    def test_live_process_after_manager_exit_keeps_scope(self):
        self.capture();self.kernel.finish();self.owners.clear()
        with patch("karyo_profiles.user_manager.uid_processes",return_value=(999,)):
            with self.assertRaises(ManagerPending):self.scope.close()
        self.assertTrue(self.scope.directory.exists())
        self.scope.close()

    def test_owner_account_change_is_refused(self):
        self.scope.prepare()
        self.scope.files.accounts.by_uid=lambda uid:LocalAccount("other",1002,1002,"/home/other","/bin/bash")
        with self.assertRaises(ManagerRetained):self.scope.close()


    def test_native_deactivation_is_pending_without_signalling_manager(self):
        self.capture()
        self.kernel.rows.update(ActiveState="deactivating",SubState="stop-sigterm",MainPID="0")
        self.kernel.alive=False
        self.owners[:]=[MANAGER]
        with self.assertRaises(ManagerPending):self.scope.close()
        self.assertFalse(any("stop" in call for call in self.kernel.calls))
        self.kernel.finish();self.owners.clear();self.scope.close()


class ManagerExecTest(unittest.TestCase):
    def test_typed_single_native_command_and_complete_argument_identity(self):
        class Command:
            def __init__(self,executable):self.executable=executable;self.calls=[]
            def run(self,*args):
                self.calls.append(args)
                if "LoadUnit" in args:return json.dumps({"type":"o","data":["/org/freedesktop/systemd1/unit/user_401002_2eservice"]})
                path,argv,ignore=self.executable
                return json.dumps({"type":"a(sasbttttuii)","data":[[path,list(argv),ignore,0,0,0,0,0,0,0]]})
        for expected in (BASE_MANAGER,SCOPED_MANAGER):
            command=Command(expected)
            self.assertEqual(manager_exec_start(command,1002),expected)
            self.assertTrue(all("--system" in call for call in command.calls))

    def test_wrong_object_multi_commands_or_malformed_signature_refused(self):
        from unittest.mock import Mock
        good={"type":"o","data":["/org/freedesktop/systemd1/unit/user_401002_2eservice"]}
        invalid=[{"type":"o","data":["/foreign"]},{"type":"a(sasbttttuii)","data":[]},
                 {"type":"a(sasbttttuii)","data":[["/bin/true",["/bin/true"],False,0,0,0,0,0,0,0]]*2},
                 {"type":"a(sasbttttuii)","data":[["/bin/true",["bad"+chr(10)],False,0,0,0,0,0,0,0]]}]
        for index,value in enumerate(invalid):
            command=Mock()
            command.run.side_effect=[json.dumps(value)] if index==0 else [json.dumps(good),json.dumps(value)]
            with self.assertRaises(ServiceError):manager_exec_start(command,1002)
