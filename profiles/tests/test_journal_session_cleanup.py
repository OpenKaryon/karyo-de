# SPDX-License-Identifier: GPL-3.0-or-later
from copy import deepcopy
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.journal import JournalRetained
from karyo_profiles.journal_session import manager_receipt
from karyo_profiles.journal_session_cleanup import closed_graph,removing_manager,recover_closed_session
from karyo_profiles.user_manager import UserManagerScope,BASE_MANAGER,SCOPED_MANAGER
from test_journal_session import active,ACCOUNT
import test_user_manager as fixtures


class ClosedGraphTest(unittest.TestCase):
    def fixture(self):
        sessions=Mock();sessions._identifiers.return_value=()
        graph=Mock();graph.info.return_value={"LoadState":"not-found"}
        return sessions,graph

    def test_both_completed_units_and_old_processes_must_be_absent(self):
        sessions,graph=self.fixture()
        with patch("karyo_profiles.journal_session_cleanup.GraphicalSession",return_value=graph), \
             patch("karyo_profiles.journal_session_cleanup.process_gone",return_value=True) as gone:
            closed_graph(Mock(),dict(active(),pending="login-close"),sessions)
        self.assertEqual(gone.call_count,2)
        self.assertEqual(graph.info.call_args_list[1].args,("session-c1.scope",))

    def test_live_old_process_or_reused_login_identifier_retains_resources(self):
        for gone,identifiers in ((False,()),(True,("c2",))):
            sessions,graph=self.fixture();sessions._identifiers.return_value=identifiers
            with patch("karyo_profiles.journal_session_cleanup.GraphicalSession",return_value=graph), \
                 patch("karyo_profiles.journal_session_cleanup.process_gone",return_value=gone):
                with self.assertRaises(JournalRetained):closed_graph(Mock(),dict(active(),pending="login-close"),sessions)

    def test_loaded_graphical_unit_requires_same_invocation_and_empty_finished_cgroup(self):
        for changes in ({"InvocationID":"ab"*16},{"ControlGroup":"/system.slice/foreign.service"},
                        {"ActiveState":"active"},{"MainPID":"345"},{"Transient":"no"}):
            sessions,graph=self.fixture()
            row={"LoadState":"loaded","InvocationID":"cd"*16,"Transient":"yes",
                 "ControlGroup":"","ActiveState":"inactive","MainPID":"0"}
            graph.info.return_value=dict(row,**changes)
            with patch("karyo_profiles.journal_session_cleanup.GraphicalSession",return_value=graph), \
                 patch("karyo_profiles.journal_session_cleanup.process_gone",return_value=True):
                with self.assertRaises(JournalRetained):closed_graph(Mock(),dict(active(),pending="login-close"),sessions)

    def test_same_completed_graphical_units_are_accepted_without_native_stop(self):
        sessions,graph=self.fixture()
        graph.info.side_effect=[{"LoadState":"loaded","InvocationID":"cd"*16,"Transient":"yes",
            "ControlGroup":"","ActiveState":"failed","MainPID":"0"},
            {"LoadState":"loaded","InvocationID":"ef"*16,"Transient":"yes","ControlGroup":"","ActiveState":"inactive"}]
        with patch("karyo_profiles.journal_session_cleanup.GraphicalSession",return_value=graph), \
             patch("karyo_profiles.journal_session_cleanup.process_gone",return_value=True):
            closed_graph(Mock(),dict(active(),pending="login-close"),sessions)
        graph.command.run.assert_not_called()

    def test_lost_graph_receipt_does_not_authorize_adopting_any_remaining_unit(self):
        sessions,graph=self.fixture();graph.info.return_value={"LoadState":"loaded"}
        with patch("karyo_profiles.journal_session_cleanup.GraphicalSession",return_value=graph), \
             patch("karyo_profiles.journal_session_cleanup.process_gone",return_value=True):
            with self.assertRaises(JournalRetained):closed_graph(Mock(),dict(active(),pending="manager-close",graphical=None),sessions)


class ExitedSessionRecoveryTest(unittest.TestCase):
    def test_exit_before_restore_records_close_before_finishing_captured_scope(self):
        record={'account':dict(name=ACCOUNT.name,uid=ACCOUNT.uid,gid=ACCOUNT.gid,home=ACCOUNT.home,shell=ACCOUNT.shell),
                'session':active(),'host_network_policy':{}}
        events=[];journal=Mock();journal.change.side_effect=lambda old,new:events.append(('intent',new['session']['pending']))
        accounts=Mock();accounts.by_uid.return_value=ACCOUNT
        sessions=Mock();sessions.owners.return_value=(Mock(),)
        with patch('karyo_profiles.journal_session_cleanup.process_gone',return_value=True), \
             patch('karyo_profiles.journal_session_cleanup.finish_abandoned_login',side_effect=lambda *a:events.append(('finish',a[0].pid))):
            from karyo_profiles.user_manager import ManagerPending
            with self.assertRaises(ManagerPending):recover_closed_session(journal,Mock(),record,accounts=accounts,sessions=sessions)
        self.assertEqual(events,[('intent','login-close'),('finish',345)])

    def test_live_compositor_remains_an_ordinary_restore(self):
        record={'account':dict(name=ACCOUNT.name,uid=ACCOUNT.uid,gid=ACCOUNT.gid,home=ACCOUNT.home,shell=ACCOUNT.shell),'session':active()}
        accounts=Mock();accounts.by_uid.return_value=ACCOUNT;journal=Mock()
        with patch('karyo_profiles.journal_session_cleanup.process_gone',return_value=False), \
             patch('karyo_profiles.journal_session_cleanup.finish_abandoned_login')as finish:
            self.assertFalse(recover_closed_session(journal,Mock(),record,accounts=accounts,sessions=Mock()))
        journal.change.assert_not_called();finish.assert_not_called()


@unittest.skipUnless(os.geteuid()==0,"real captured manager policy files")
class ClosingManagerFilesTest(unittest.TestCase):
    def setUp(self):
        fixtures.ManagerScopeTest.setUp(self)
        def executable(*unused):
            return SCOPED_MANAGER if self.kernel.rows["NetworkNamespacePath"] else BASE_MANAGER
        for item in (patch("karyo_profiles.user_manager.manager_exec_start",side_effect=executable),
                     patch("karyo_profiles.journal_session_cleanup.manager_exec_start",side_effect=executable)):
            item.start();self.addCleanup(item.stop)
        self.scope.prepare();self.proof=manager_receipt(self.scope)
        self.original_directory=self.scope.directory
        def factory(files,**kwargs):
            value=UserManagerScope(files,sessions=self.sessions,command=self.kernel,process=self.kernel,**kwargs)
            value.directory=self.original_directory
            return value
        item=patch("karyo_profiles.journal_session_cleanup.UserManagerScope",side_effect=factory)
        item.start();self.addCleanup(item.stop)

    def release(self):fixtures.ManagerScopeTest.release(self)
    def restore(self,allow_missing=True):return removing_manager(self.scope.files,self.proof,allow_missing=allow_missing)
    def close_restored(self):
        manager=self.restore()
        try:manager.close()
        finally:
            if manager.fd is not None:os.close(manager.fd);manager.fd=None
        self.assertFalse(self.original_directory.exists())
        self.assertFalse(any("stop" in call for call in self.kernel.calls))

    def test_full_captured_policy_can_finish_recorded_close(self):self.close_restored()

    def test_unlinked_file_finishes_only_the_same_empty_directory(self):
        (self.original_directory/self.scope.filename).unlink()
        self.close_restored()

    def test_directory_already_removed_finishes_reload_without_a_fake_fd(self):
        (self.original_directory/self.scope.filename).unlink();self.original_directory.rmdir()
        self.close_restored()

    def test_already_reloaded_absent_policy_is_idempotently_acknowledged(self):
        self.scope.close();self.close_restored()

    def test_missing_file_without_removal_intent_is_retained(self):
        (self.original_directory/self.scope.filename).unlink()
        with self.assertRaises(JournalRetained):self.restore(allow_missing=False)
        self.assertTrue(self.original_directory.exists())

    def test_replaced_directory_is_never_removed_by_name(self):
        self.original_directory.rename(self.original_directory.with_suffix(".saved"))
        self.original_directory.mkdir()
        with self.assertRaises(JournalRetained):self.restore()
        self.assertTrue(self.original_directory.exists())

    def test_foreign_files_and_changed_captured_bytes_are_retained(self):
        target=self.original_directory/self.scope.filename;original=target.read_bytes()
        target.write_bytes(original+b"changed")
        with self.assertRaises(RuntimeError):self.restore()
        target.write_bytes(original);foreign=self.original_directory/"foreign";foreign.write_text("keep")
        with self.assertRaises(JournalRetained):self.restore()
        self.assertEqual(foreign.read_text(),"keep")

    def test_foreign_native_policy_prevents_empty_directory_removal(self):
        (self.original_directory/self.scope.filename).unlink()
        self.kernel.rows["NetworkNamespacePath"]="/run/netns/unrelated"
        with self.assertRaises(RuntimeError):self.restore()
        self.assertTrue(self.original_directory.exists())


if __name__=="__main__":unittest.main()
