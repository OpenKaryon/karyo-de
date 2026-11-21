# SPDX-License-Identifier: GPL-3.0-or-later
from dataclasses import replace
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.graphical import GraphicalSession,GraphicalRetained
from karyo_profiles.logind import LoginOwner,Session
from karyo_profiles.networks import NetworkSpec
from karyo_profiles.services import ServiceError
from karyo_profiles.session_entry import main,wait_active

HOME=LocalAccount("home",1001,1001,"/home/home","/bin/bash")
WORK=LocalAccount("work",1002,1002,"/home/work","/bin/bash")
LOGIN=LoginOwner("c1","work",1002,345,"user","session-c1.scope","karyo-desktop","online")
MANAGER=LoginOwner("c2","work",1002,346,"manager","","systemd-user","active")


class EntryTest(unittest.TestCase):
    def library(self,uid=1002,active=(0,1),result=0):
        def owner(name,pointer):pointer._obj.value=uid;return result
        return SimpleNamespace(sd_session_get_uid=Mock(side_effect=owner),
                               sd_session_is_active=Mock(side_effect=active))
    def test_inactive_session_waits_without_activating_or_executing_gui(self):
        sleep=Mock()
        wait_active("c1",1002,library=self.library(active=(0,0,1)),clock=lambda:0,sleep=sleep)
        self.assertEqual(sleep.call_count,2)
    def test_changed_uid_missing_session_and_timeout_are_closed(self):
        for library,clock,error in ((self.library(uid=1003),lambda:0,PermissionError),
            (self.library(result=-1),lambda:0,PermissionError),
            (self.library(active=(-1,)),lambda:0,RuntimeError),
            (self.library(active=(0,)),iter((0,60)).__next__,TimeoutError)):
            with self.assertRaises(error):wait_active("c1",1002,library=library,clock=clock,sleep=lambda x:None)
    def test_entry_executes_fixed_program_only_after_own_login_activation(self):
        events=[]
        with patch("karyo_profiles.session_entry.os.getuid",return_value=1002), \
             patch("karyo_profiles.session_entry.os.geteuid",return_value=1002), \
             patch("karyo_profiles.session_entry.process_session",return_value="c1"), \
             patch("karyo_profiles.session_entry.wait_active",side_effect=lambda *a:events.append("wait")), \
             patch("karyo_profiles.session_entry.os.execv",side_effect=lambda *a:events.append(a)), \
             patch.object(sys,"argv",["profile-session-wait"]):
            main("/usr/bin/karyo-session")
        self.assertEqual(events,["wait",("/usr/bin/karyo-session",["/usr/bin/karyo-session"])])
    def test_root_or_arguments_never_reach_gui(self):
        with patch("karyo_profiles.session_entry.os.execv") as execute, \
             patch("karyo_profiles.session_entry.os.getuid",return_value=0), \
             patch.object(sys,"argv",["entry"]):
            self.assertEqual(main("/usr/bin/karyo-session"),1)
            execute.assert_not_called()


class Kernel:
    def __init__(self):
        self.present=False;self.scope=False;self.alive=False;self.calls=[];self.owners=[]
        self.invocation="ab"*16;self.scope_invocation="cd"*16;self.pid="345"
        self.sessions={"s1":Session("s1","home",1001,8,True)}
        self.events=[];self.failure=False;self.closed=[]
    def run(self,*args,**kwargs):
        self.calls.append(args)
        if args[0].endswith("systemd-run"):
            self.present=self.scope=self.alive=True;self.owners[:]=[LOGIN,MANAGER]
            self.sessions["c1"]=Session("c1","work",1002,9,False)
            if self.failure:raise TimeoutError("Lost acknowledgement")
            return ""
        if args[0]=="terminate-session":
            self.events.append("terminate:"+args[1]);self.scope=self.alive=False
            self.owners[:]=[MANAGER];self.sessions.pop("c1",None);return ""
        if "stop" in args:
            self.present=False;return ""
        scope=args[2].startswith("session-")
        present=self.scope if scope else self.present
        rows=dict(LoadState="loaded" if present else "not-found",
                  InvocationID=self.scope_invocation if scope else self.invocation,
                  ControlGroup="/user.slice/user-1002.slice/session-c1.scope" if scope else
                    "/system.slice/karyo-session-"+"ab"*16+".service",
                  ActiveState="active" if present else "inactive",Transient="yes",
                  MainPID=self.pid if self.alive else "0")
        return "".join(key+"="+rows[key]+"\n" for key in (arg.split("=",1)[1] for arg in args if arg.startswith("--property=")))
    def owners_for(self,uid):return tuple(self.owners)
    def get(self,identifier,timeout=5):return self.sessions.get(identifier)
    def all(self):return tuple(self.sessions.values())
    def lock(self,identifier,timeout=2):
        self.events.append("lock:"+identifier)
        self.sessions[identifier]=replace(self.sessions[identifier],locked=True)
    def activate(self,identifier,timeout=2):
        self.events.append("activate:"+identifier)
        self.sessions={key:replace(value,active=key==identifier) for key,value in self.sessions.items()}
    def open(self,pid):return 123
    def exited(self,fd,seconds=0):return not self.alive
    def close(self,fd):self.closed.append(fd)


class GraphicalTest(unittest.TestCase):
    def setUp(self):
        self.kernel=Kernel()
        self.store=SimpleNamespace(_locked=True,ownership=lambda:SimpleNamespace(
            family=lambda name:("home","work") if name in {"home","work"} else ("other",)),
            settings=lambda name:SimpleNamespace(password="home" if name=="home" else "none"))
        accounts=SimpleNamespace(by_uid=lambda uid:{1001:HOME,1002:WORK}[uid],
            expiry_day=lambda name:None,password_hash=lambda name:"fixture-hash")
        def verify():
            if not self.store._locked:raise PermissionError("Lock no longer held")
        network=SimpleNamespace(store=self.store,spec=NetworkSpec(1002,"ab"*16),command=self.kernel,identity=(4,5))
        files=SimpleNamespace(network=network,accounts=accounts,verify=verify,
            plan=SimpleNamespace(client_properties=lambda:("NetworkNamespacePath=/run/netns/fixture",)))
        manager=SimpleNamespace(files=files,network=network,account=WORK,environment=(),
            verify_file=lambda:None,info=lambda:{},verify_policy=lambda rows:None,
            inactive=lambda rows:True,capture=Mock(),verify=lambda:None)
        sessions=SimpleNamespace(owners=self.kernel.owners_for,get=self.kernel.get,all=self.kernel.all,
            lock=self.kernel.lock,activate=self.kernel.activate,command=self.kernel)
        self.lease=GraphicalSession(manager,sessions=sessions,command=self.kernel,process=self.kernel)
        self.console=SimpleNamespace(store=self.store,vt=9,path="/dev/tty9",verify=Mock())
        patches=(patch("karyo_profiles.graphical.host_controller"),
                 patch("karyo_profiles.graphical.uid_processes",return_value=()),
                 patch("karyo_profiles.graphical.program_identity",return_value=(4,50)),
                 patch.object(self.lease,"process_start",return_value=456),
                 patch.object(self.lease,"waiting",return_value=True))
        for item in patches:item.start();self.addCleanup(item.stop)

    def start(self):self.lease.start(self.console)
    def test_capture_owns_pam_scope_without_initial_activation(self):
        self.start();self.lease.verify()
        self.console.verify.assert_called_once()
        self.assertFalse(self.kernel.events)
        self.assertEqual(self.lease.identity.login,LOGIN)
        native=next(c for c in self.kernel.calls if c[0].endswith("systemd-run"))
        self.assertIn("--property=TTYVHangup=no",native)
        self.assertIn("--property=StandardInput=tty-fail",native)

    def test_source_protection_is_locked_before_entering_new_profile(self):
        self.start();result=self.lease.enter(HOME)
        self.assertEqual(self.kernel.events,["lock:s1","activate:c1"])
        self.assertEqual(result,{"user":"work","vt":9,"changed":True,"starting":True})

    def test_cross_family_never_activates_or_locks(self):
        self.start()
        with self.assertRaises(PermissionError):
            self.lease.enter(LocalAccount("other",1003,1003,"/home/other","/bin/bash"))
        self.assertFalse(self.kernel.events)

    def test_lost_start_acknowledgement_is_retained_without_name_kill(self):
        self.kernel.failure=True
        with self.assertRaises(GraphicalRetained):self.start()
        with self.assertRaises(GraphicalRetained):self.lease.close()
        self.assertFalse(self.kernel.events)
        self.assertTrue(self.kernel.alive)

    def test_replaced_service_or_scope_refuses_every_termination(self):
        self.start()
        self.kernel.scope_invocation="ef"*16
        with self.assertRaises(GraphicalRetained):self.lease.close()
        self.assertFalse(self.kernel.events)
        self.kernel.scope_invocation="cd"*16;self.kernel.invocation="ef"*16
        with self.assertRaises(GraphicalRetained):self.lease.close()
        self.assertFalse(self.kernel.events)

    def test_changed_logind_leader_cannot_be_terminated(self):
        self.start()
        self.kernel.owners[0]=replace(LOGIN,leader=999)
        with self.assertRaises(GraphicalRetained):self.lease.close()
        self.assertFalse(self.kernel.events)

    def test_close_terminates_only_captured_login_and_service_then_is_idempotent(self):
        self.start();self.lease.close();self.lease.close()
        self.assertEqual(self.kernel.events,["terminate:c1"])
        self.assertEqual(self.kernel.closed,[123])
        self.assertTrue(self.lease.closed)

    def test_dead_leader_with_own_live_scope_still_uses_logind_teardown(self):
        self.start();self.kernel.alive=False
        self.lease.close()
        self.assertEqual(self.kernel.events,["terminate:c1"])

    def test_expired_account_and_occupied_unit_never_start(self):
        self.lease.files.accounts.expiry_day=lambda name:1
        with self.assertRaises(PermissionError):self.start()
        self.assertFalse(self.kernel.present)
        self.lease.files.accounts.expiry_day=lambda name:None;self.kernel.present=True
        with self.assertRaises(ServiceError):self.start()
        self.assertFalse(any(c[0].endswith("systemd-run") for c in self.kernel.calls))

    def test_unheld_lock_refuses_mutation(self):
        self.store._locked=False
        with self.assertRaises(PermissionError):self.start()
        self.assertFalse(self.kernel.calls)
