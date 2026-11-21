# SPDX-License-Identifier: GPL-3.0-or-later
from copy import deepcopy
from dataclasses import asdict
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.journal import JournalRetained
from karyo_profiles.journal_session import JournalSession,SessionRegistry,recover_prepared_session
from karyo_profiles.journal_session_record import validate_session
from karyo_profiles.logind import LoginOwner

ACCOUNT=LocalAccount("work",1002,1002,"/home/work","/bin/bash")
LOGIN=LoginOwner("c1","work",1002,345,"user","session-c1.scope","karyo-desktop","online")
MANAGER=LoginOwner("c2","work",1002,346,"manager","","systemd-user","active")


def prepared():return {"directory":[2,3],"file":[2,4],"dropins":["/usr/lib/systemd/system/user@.service.d/native.conf"],"live":None}
def active():
    manager=prepared();manager["live"]={"invocation":"ab"*16,"pid":346,"start":600,"exe":[2,60],"login":asdict(LOGIN),"manager_login":asdict(MANAGER)}
    graph={"invocation":"cd"*16,"pid":345,"start":599,"login":asdict(LOGIN),"scope_invocation":"ef"*16,
           "vt":9,"waiter":[2,70],"compositor":[2,71],"entered":True}
    return {"pending":None,"manager":manager,"graphical":graph}


class SessionSchemaTest(unittest.TestCase):
    def test_preparation_live_entry_and_closing_have_explicit_shapes(self):
        for value in ({"pending":"manager-prepare","manager":None,"graphical":None},
                      {"pending":None,"manager":prepared(),"graphical":None},active(),
                      dict(active(),pending="enter"),dict(active(),pending="login-close"),
                      dict(active(),pending="manager-close",graphical=None)):
            validate_session(value,ACCOUNT)

    def test_graphical_and_manager_login_scope_owner_and_pid_cannot_disagree(self):
        for path,value in ((["graphical","pid"],346),(["graphical","vt"],7),
            (["graphical","scope_invocation"],"invalid"),(["graphical","login","leader"],999),
            (["graphical","login","uid"],"1002"),(["manager","live","manager_login","scope"],"session-c2.scope"),
            (["manager","live","login","service"],"sshd")):
            candidate=active();target=candidate
            for part in path[:-1]:target=target[part]
            target[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(ValueError):validate_session(candidate,ACCOUNT)

    def test_unrecorded_commands_paths_and_ambiguous_preparation_are_rejected(self):
        for candidate in (dict(active(),program="/bin/sh"),dict(active(),pending="manager-prepare"),
                          {"pending":None,"manager":None,"graphical":None}):
            with self.assertRaises(ValueError):validate_session(candidate,ACCOUNT)
        candidate=active();candidate["manager"]["dropins"]=["/tmp/with space"]
        with self.assertRaises(ValueError):validate_session(candidate,ACCOUNT)


class ConsumersTest(unittest.TestCase):
    def scope(self,pids=None,group=None,namespace=None):
        own=Path("/proc/self/cgroup").read_text().strip()[3:]
        info=os.stat("/proc/self/ns/net")
        network=SimpleNamespace(identity=(info.st_dev,info.st_ino) if namespace is None else namespace,
            name="known-namespace",_ip=Mock(return_value=str(os.getpid())+"\n" if pids is None else pids))
        dns=SimpleNamespace(network=SimpleNamespace(network=network),token="ab"*16,
            services=SimpleNamespace(verify=Mock(),resolver=SimpleNamespace(identity=SimpleNamespace(pid=2147483647))))
        manager=SimpleNamespace(verify=Mock(),identity=SimpleNamespace(cgroup="/unrelated/user@1002.service/init.scope"))
        graph=SimpleNamespace(name="karyo-session-fixture.service",verify=Mock(),identity=SimpleNamespace(cgroup=own if group is None else group))
        return JournalSession(Mock(),dns,manager,graph)

    def test_actual_current_pid_is_observed_without_signal_under_its_captured_cgroup(self):
        scope=self.scope();scope.verify_consumers()
        self.assertEqual(scope.manager.verify.call_count,2)
        self.assertEqual(scope.graph.verify.call_count,2)

    def test_unknown_scope_or_namespace_is_not_accepted_as_a_consumer(self):
        for scope in (self.scope(group="/unknown/session-c1.scope"),self.scope(namespace=(99,99))):
            with self.assertRaises(JournalRetained):scope.verify_consumers()

    def test_malformed_pid_inventory_and_changed_anchor_fail_before_use(self):
        for values in ("001\n","--help\n","1\n"):
            with self.assertRaises(JournalRetained):self.scope(pids=values).verify_consumers()
        scope=self.scope();scope.manager.verify.side_effect=JournalRetained("Changed manager")
        with patch("os.pidfd_open") as opened:
            with self.assertRaises(JournalRetained):scope.verify_consumers()
            opened.assert_not_called()

    def test_process_exit_retries_a_complete_bounded_inventory(self):
        scope=self.scope()
        with patch("os.pidfd_open",side_effect=ProcessLookupError()):
            with self.assertRaises(JournalRetained):scope.verify_consumers()
        self.assertEqual(scope.network.network._ip.call_count,3)


class PreparedRecoveryTest(unittest.TestCase):
    def fixture(self,pending=None):
        record={"token":"ab"*16,"account":asdict(ACCOUNT),"session":{"pending":pending,"manager":prepared(),"graphical":None}}
        class Journal:
            def __init__(self):self.current=deepcopy(record)
            def record(self,token):return deepcopy(self.current)
            def change(self,before,after):
                if before!=self.current:raise JournalRetained("Record changed")
                self.current=deepcopy(after)
        return record,Journal()

    def run_recovery(self,record,journal,*,unit="not-found",closing_error=None):
        from contextlib import ExitStack
        accounts=Mock();accounts.by_uid.return_value=ACCOUNT
        network=SimpleNamespace(fd=None,spec=SimpleNamespace(token=record["token"]))
        dns=Mock();dns.token=record["token"];dns.network=SimpleNamespace(network=network)
        manager=Mock();manager.fd=manager.pidfd=None
        def close():
            self.assertEqual(journal.current["session"]["pending"],"manager-close")
            if closing_error:raise closing_error
        manager.close.side_effect=close
        graph=Mock();graph.info.return_value={"LoadState":unit}
        with ExitStack() as stack:
            stack.enter_context(patch("karyo_profiles.journal_session.offline"))
            stack.enter_context(patch("karyo_profiles.journal_session.restore_network",return_value=network))
            stack.enter_context(patch("karyo_profiles.journal_session.JournalDNS.restore",return_value=dns))
            stack.enter_context(patch("karyo_profiles.journal_session.restore_manager",return_value=manager))
            stack.enter_context(patch("karyo_profiles.journal_session.GraphicalSession",return_value=graph))
            try:result=recover_prepared_session(journal,Mock(),record,accounts=accounts,sessions=Mock())
            finally:self.dns,self.manager=dns,manager
        return result

    def test_acknowledged_preparation_and_unstarted_login_intent_remove_only_manager_receipt(self):
        for pending in (None,"login-start"):
            record,journal=self.fixture(pending)
            self.assertTrue(self.run_recovery(record,journal))
            self.assertNotIn("session",journal.current)
            self.manager.close.assert_called_once();self.dns.close.assert_not_called()
            self.dns.release_handles.assert_called_once()

    def test_unrecorded_graphical_unit_retains_receipt_without_policy_or_dns_close(self):
        record,journal=self.fixture("login-start")
        with self.assertRaises(JournalRetained):self.run_recovery(record,journal,unit="loaded")
        self.assertEqual(journal.current,record)
        self.manager.close.assert_not_called();self.dns.close.assert_not_called()
        self.dns.release_handles.assert_called_once()

    def test_failed_policy_removal_keeps_written_close_intent(self):
        record,journal=self.fixture()
        with self.assertRaises(JournalRetained):self.run_recovery(record,journal,closing_error=JournalRetained("Changed policy"))
        self.assertEqual(journal.current["session"]["pending"],"manager-close")
        self.dns.close.assert_not_called();self.dns.release_handles.assert_called_once()

    def test_live_incomplete_and_closing_sessions_are_not_misclassified_as_prepared(self):
        accounts=Mock();accounts.by_uid.return_value=ACCOUNT
        for value in (active(),dict(active(),pending="login-close"),
                      {"pending":"manager-prepare","manager":None,"graphical":None},
                      {"pending":"manager-close","manager":prepared(),"graphical":None}):
            record={"account":asdict(ACCOUNT),"session":value}
            with patch("karyo_profiles.journal_session.restore_network") as network:
                self.assertFalse(recover_prepared_session(Mock(),Mock(),record,accounts=accounts))
                network.assert_not_called()

    def test_changed_account_and_live_owner_fail_before_namespace_adoption(self):
        record,journal=self.fixture();accounts=Mock();accounts.by_uid.return_value=ACCOUNT
        with patch("karyo_profiles.journal_session.offline",side_effect=JournalRetained("A login exists")), \
             patch("karyo_profiles.journal_session.restore_network") as network:
            with self.assertRaises(JournalRetained):recover_prepared_session(journal,Mock(),record,accounts=accounts)
            network.assert_not_called()
        record["account"]["home"]="/home/changed"
        with self.assertRaises(JournalRetained):recover_prepared_session(journal,Mock(),record,accounts=accounts)


class RegistryTest(unittest.TestCase):
    @patch("karyo_profiles.journal_session.recover_prepared_session",return_value=False)
    def test_only_restored_session_tokens_are_excluded_from_legacy_cleanup(self,prepared_recovery):
        journal=Mock();store=Mock()
        from contextlib import nullcontext
        store.locked.side_effect=lambda:nullcontext(store)
        record={"token":"ab"*16,"session":active()}
        journal.snapshot.return_value={"profiles":[record,{"token":"cd"*16}]}
        owned=Mock()
        with patch.object(JournalSession,"restore",return_value=owned) as restore, \
             patch("karyo_profiles.journal_session.recover_profiles") as cleanup:
            registry=SessionRegistry(journal,store).restore()
            restore.assert_called_once_with(journal,store,record)
            self.assertEqual(set(cleanup.call_args.kwargs["exclude"]),{"ab"*16})
            owned.release_handles.assert_not_called()
            registry.release_handles();owned.release_handles.assert_called_once()

    @patch("karyo_profiles.journal_session.recover_prepared_session",return_value=False)
    def test_failed_later_restore_releases_only_observers_of_already_captured_sessions(self,prepared_recovery):
        from contextlib import nullcontext
        journal=Mock();store=Mock();store.locked.side_effect=lambda:nullcontext(store)
        journal.snapshot.return_value={"profiles":[{"token":"ab"*16,"session":active()},{"token":"cd"*16,"session":active()}]}
        owned=Mock()
        with patch.object(JournalSession,"restore",side_effect=[owned,JournalRetained("Unknown second session")]), \
             patch("karyo_profiles.journal_session.recover_profiles") as cleanup:
            with self.assertRaises(JournalRetained):SessionRegistry(journal,store).restore()
            owned.release_handles.assert_called_once();owned.close.assert_not_called();cleanup.assert_not_called()


if __name__=="__main__":unittest.main()
