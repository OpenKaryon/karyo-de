# SPDX-License-Identifier: GPL-3.0-or-later
from contextlib import nullcontext
from dataclasses import asdict,replace
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock,Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.logind import Greeter,Session,SessionError,Sessions,LoginOwner
from karyo_profiles.model import DesktopSettings,Ownership
from karyo_profiles.returning import ReturnAfterExit
from karyo_profiles.journal_session import SessionRegistry
from karyo_profiles.user_manager import ManagerPending
from karyo_profiles.dispatcher import serve
from test_logind import properties

HOME=LocalAccount("home",1001,1001,"/home/home","/bin/bash")
WORK=LocalAccount("work",1002,1002,"/home/work","/bin/bash")
LOGIN=LoginOwner("c1","work",1002,345,"user","session-c1.scope","karyo-desktop","closing")


class MetadataTest(unittest.TestCase):
    def test_only_local_service_greeter_metadata_is_selectable(self):
        text=properties(identifier="g1",name="lightdm",uid="112",Class="greeter",VTNr="4")
        self.assertEqual(Greeter.parse(text,"g1"),Greeter("g1","lightdm",112,4,True))
        base={"identifier":"g1","name":"lightdm","uid":"112","Class":"greeter","VTNr":"4"}
        for changes in ({"Remote":"yes"},{"Seat":"seat1"},{"Type":"tty"},{"Class":"user"}):
            self.assertIsNone(Greeter.parse(properties(**(base|changes)),"g1"))

    def test_greeter_uid_vt_duplicate_or_unknown_properties_are_rejected(self):
        for uid,vt in (("0","4"),("1000","4"),("0112","4"),("112","0"),("112","64")):
            with self.assertRaises(ValueError):Greeter.parse(properties(identifier="g1",name="lightdm",uid=uid,Class="greeter",VTNr=vt),"g1")
        text=properties(identifier="g1",name="lightdm",uid="112",Class="greeter")
        for suffix in ("Name=other\n","Unknown=yes\n"):
            with self.assertRaises(ValueError):Greeter.parse(text+suffix,"g1")

    def test_seat_active_is_a_unique_bounded_native_identifier(self):
        command=Mock();sessions=Sessions(command)
        for text,wanted in (("Id=seat0\nActiveSession=c1\n","c1"),("Id=seat0\nActiveSession=\n","")):
            command.run.return_value=text;self.assertEqual(sessions.seat_active(),wanted)
        for text in ("Id=seat1\nActiveSession=c1\n","Id=seat0\n","Id=seat0\nActiveSession=--bad\n","Id=seat0\nActiveSession=c1\nActiveSession=c2\n"):
            command.run.return_value=text
            with self.assertRaises(ValueError):sessions.seat_active()


class ReturnTest(unittest.TestCase):
    def setUp(self):
        self.time=0.;self.active_vt=9;self.active_id="c1";self.calls=[];self.confirm=True
        self.kernel=MagicMock();self.kernel.__enter__=Mock(return_value=self.kernel);self.kernel.__exit__=Mock(return_value=False)
        self.kernel.state.side_effect=lambda:SimpleNamespace(active=self.active_vt)
        self.store=Mock();self.store._locked=True
        self.store.ownership.return_value=Ownership.parse("home work\n")
        self.store.settings.return_value=DesktopSettings()
        self.accounts=Mock();self.accounts.by_uid.side_effect=lambda uid:{1001:HOME,1002:WORK}[uid]
        self.accounts.account.return_value=HOME;self.accounts.expiry_day.return_value=None
        self.accounts.password_hash.return_value="$y$synthetic-fixture"
        self.home=Session("h1","home",1001,8,False);self.greeter=Greeter("g1","lightdm",112,4,False)
        self.sessions=Mock();self.sessions.all.side_effect=lambda:[self.home]
        self.sessions.get.side_effect=lambda identifier,**unused:self.home
        self.sessions.greeter.side_effect=lambda identifier,**unused:self.greeter
        self.sessions.greeters.return_value=[self.greeter]
        self.sessions.seat_active.side_effect=lambda:self.active_id
        self.sessions.owner.return_value=LOGIN
        self.sessions.lock.side_effect=self.lock
        self.sessions.activate.side_effect=self.activate
        graph=SimpleNamespace(vt=9,account=WORK,pidfd=123,process=SimpleNamespace(exited=Mock(return_value=True)),
            identity=SimpleNamespace(pid=345,start=599,invocation="cd"*16,login=LOGIN))
        self.scope=SimpleNamespace(graph=graph,token="ab"*16,journal=Mock())
        self.scope.journal.record.return_value={"account":asdict(WORK),"session":{"graphical":{"pid":345,"start":599,"invocation":"cd"*16}}}
        self.returner=ReturnAfterExit(self.store,self.accounts,self.sessions,kernel=self.kernel,clock=lambda:self.time,sleep=self.sleep)

    def sleep(self,value):self.time+=value
    def lock(self,identifier,**unused):
        self.calls.append(("lock",identifier))
        if self.confirm:self.home=replace(self.home,locked=True)
    def activate(self,identifier,**unused):
        self.calls.append(("activate",identifier));self.active_id=identifier
        if identifier=="h1":self.home=replace(self.home,active=True);self.active_vt=self.home.vt
        else:self.greeter=replace(self.greeter,active=True);self.active_vt=self.greeter.vt
    def run_return(self):return self.returner.run(self.scope)

    def test_home_return_uses_its_actual_session_and_vt(self):
        result=self.run_return()
        self.assertTrue(result["changed"]);self.assertEqual(result["vt"],8)
        self.assertEqual(self.calls,[("activate","h1")])

    def test_protected_home_confirms_lock_before_activation(self):
        self.store.settings.return_value=DesktopSettings(password="home")
        self.run_return();self.assertEqual(self.calls,[("lock","h1"),("activate","h1")])

    def test_missing_home_uses_verified_native_greeter_without_a_fixed_console(self):
        self.sessions.all.return_value=[];self.sessions.all.side_effect=None
        result=self.run_return();self.assertTrue(result["greeter"]);self.assertEqual(result["vt"],4)
        self.assertEqual(self.calls,[("activate","g1")])

    def test_no_unique_destination_does_not_activate_an_arbitrary_console(self):
        self.sessions.all.side_effect=None;self.sessions.all.return_value=[];self.sessions.greeters.return_value=[]
        with self.assertRaises(SessionError):self.run_return()
        self.assertEqual(self.calls,[])

    def test_another_seat_session_or_kernel_console_prevents_return(self):
        for seat,vt in (("other",9),("c1",10),("h1",8)):
            self.active_id,self.active_vt=seat,vt
            self.assertFalse(self.run_return()["changed"])
        self.assertEqual(self.calls,[])

    def test_reused_departing_logind_identifier_is_not_owned(self):
        self.sessions.owner.return_value=replace(LOGIN,leader=999)
        self.assertFalse(self.run_return()["changed"]);self.assertEqual(self.calls,[])

    def test_seat_changed_during_background_lock_is_not_stolen(self):
        self.store.settings.return_value=DesktopSettings(password="home")
        def lock(*args,**kwargs):self.lock(*args,**kwargs);self.active_id="other"
        self.sessions.lock.side_effect=lock
        self.assertFalse(self.run_return()["changed"]);self.assertEqual(self.calls,[("lock","h1")])

    def test_missing_lock_ack_times_out_without_activation(self):
        self.store.settings.return_value=DesktopSettings(password="home");self.confirm=False
        with self.assertRaises(TimeoutError):self.run_return()
        self.assertFalse(any(call[0]=="activate" for call in self.calls))

    def test_changed_target_identity_is_rejected_before_activation(self):
        self.sessions.get.return_value=replace(self.home,uid=1007);self.sessions.get.side_effect=None
        with self.assertRaises(SessionError):self.run_return()
        self.assertEqual(self.calls,[])

    def test_live_process_changed_receipt_or_missing_lock_cannot_trigger_return(self):
        self.scope.graph.process.exited.return_value=False
        with self.assertRaises(PermissionError):self.run_return()
        self.scope.graph.process.exited.return_value=True;self.scope.journal.record.return_value["account"]["uid"]=1009
        with self.assertRaises(PermissionError):self.run_return()
        self.store._locked=False
        with self.assertRaises(PermissionError):self.run_return()
        self.assertEqual(self.calls,[])


class PollTest(unittest.TestCase):
    def fixture(self):
        store=Mock();store.locked.side_effect=lambda:nullcontext(store)
        registry=SessionRegistry(Mock(),store);scope=Mock();scope.graph.closed=False
        registry.profiles={"token":scope}
        return registry,scope

    def test_living_profile_is_not_touched(self):
        registry,scope=self.fixture();scope.graph.process.exited.return_value=False
        with patch("karyo_profiles.returning.ReturnAfterExit") as returned:registry.poll();returned.assert_not_called()
        scope.close.assert_not_called();scope.release_handles.assert_not_called()

    def test_pending_native_manager_is_retried_without_repeating_return_or_closing_dns(self):
        registry,scope=self.fixture();scope.graph.process.exited.return_value=True
        scope.close.side_effect=[ManagerPending("Natural exit"),None]
        with patch("karyo_profiles.returning.ReturnAfterExit") as returned:
            registry.poll();scope.dns.close.assert_not_called();scope.network.close.assert_not_called()
            registry.poll();returned.return_value.run.assert_called_once_with(scope)
        self.assertFalse(registry.profiles);scope.release_handles.assert_called_once()
        scope.dns.close.assert_called_once();scope.network.close.assert_called_once()

    def test_unknown_close_failure_preserves_lower_resources(self):
        registry,scope=self.fixture();scope.graph.process.exited.return_value=True;scope.close.side_effect=RuntimeError("Unknown")
        with patch("karyo_profiles.returning.ReturnAfterExit"):
            with self.assertRaises(RuntimeError):registry.poll()
        scope.dns.close.assert_not_called();scope.network.close.assert_not_called()
        self.assertIn("token",registry.profiles)

    def test_host_polls_without_clients_and_dispatches_only_readable_listener(self):
        registry=Mock();listener=Mock();connection=MagicMock()
        listener.accept.return_value=(connection,None)
        ready=Mock(side_effect=[([],[],[]),([listener],[],[]),RuntimeError("End fixture")])
        dispatcher=Mock()
        with self.assertRaisesRegex(RuntimeError,"End fixture"):serve(listener,dispatcher,registry,ready=ready)
        self.assertEqual(registry.poll.call_count,3);dispatcher.handle.assert_called_once_with(connection)


if __name__=="__main__":unittest.main()
