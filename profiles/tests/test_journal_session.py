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
from karyo_profiles.journal_session import JournalSession,SessionRegistry
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


class RegistryTest(unittest.TestCase):
    def test_only_restored_session_tokens_are_excluded_from_legacy_cleanup(self):
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

    def test_failed_later_restore_releases_only_observers_of_already_captured_sessions(self):
        from contextlib import nullcontext
        journal=Mock();store=Mock();store.locked.side_effect=lambda:nullcontext(store)
        journal.snapshot.return_value={"profiles":[{"token":"ab"*16,"session":active()},{"token":"cd"*16,"session":active()}]}
        owned=Mock()
        with patch.object(JournalSession,"restore",side_effect=[owned,JournalRetained("Unknown second session")]), \
             patch("karyo_profiles.journal_session.recover_profiles") as cleanup:
            with self.assertRaises(JournalRetained):SessionRegistry(journal,store).restore()
            owned.release_handles.assert_called_once();owned.close.assert_not_called();cleanup.assert_not_called()


if __name__=="__main__":unittest.main()
