# SPDX-License-Identifier: GPL-3.0-or-later
"""Functional publication and interrupted-retirement recovery with real files."""
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.host_network_policy import HostNetworkPolicy, LOADER_CONTENTS
from karyo_profiles.networks import NetworkSpec
from karyo_profiles.storage import ConfigStore
from test_journal import HOME, WORK, JournalFixture
from test_journal_dns import configuration, proof


@unittest.skipUnless(os.geteuid()==0, 'root-owned policy publication and journal')
class PolicyLifecycleTest(JournalFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.store=ConfigStore(runtime=self.root/'runtime')
        held=self.store.locked();held.__enter__();self.addCleanup(held.__exit__,None,None,None)
        self.directory=self.root/'rules';self.loader=self.root/'loader.conf'
        self.loader.write_text(LOADER_CONTENTS);self.loader.chmod(0o644)
        self.command=Mock();self.accounts=Mock();self.accounts.by_uid.side_effect=lambda uid:{HOME.uid:HOME,WORK.uid:WORK}[uid]
        self.sessions=Mock()
        for item in (patch('karyo_profiles.host_network_policy.DIRECTORY',self.directory),
                     patch('karyo_profiles.host_network_policy.LOADER',self.loader),
                     patch('karyo_profiles.host_network_policy.offline'),
                     patch('karyo_profiles.logind.Sessions',return_value=self.sessions)):
            item.start();self.addCleanup(item.stop)
        self.dns=self.context(HOME)

    def context(self, account):
        token=self.journal.begin_main(account);record=self.journal.record(token)
        network={'namespace':[4,100+account.uid],'indices':[9,2],
                 'macs':['02:00:00:00:00:01','02:00:00:00:00:02']}
        self.journal.change(record,dict(record,pending=None,network=network))
        dns=configuration();dns.update(pending=None,files={'directory':[2,3],
            'files':{'bus.conf':[2,4],'resolved.conf':[2,5]}},bus=proof(),resolver=dict(proof(),runtime=[[1,5],[1,6]]))
        dns['gid']=account.gid
        record=self.journal.record(token);self.journal.change(record,dict(record,dns=dns))
        native=SimpleNamespace(spec=NetworkSpec(account.uid,token),store=self.store,command=self.command)
        return SimpleNamespace(token=token,network=SimpleNamespace(network=native),
            files=SimpleNamespace(accounts=self.accounts,verify=Mock()),services=SimpleNamespace(verify=Mock()))

    def restore(self, dns=None):
        dns=self.dns if dns is None else dns
        owner=HostNetworkPolicy.restore(self.journal,dns.network.network,accounts=self.accounts,sessions=self.sessions)
        self.addCleanup(owner.release_handles)
        return owner

    def test_ready_rule_reopens_without_republication_then_retires(self):
        owner=HostNetworkPolicy.start(self.journal,self.dns);self.addCleanup(owner.release_handles)
        before=(self.directory/owner.name).stat().st_ino
        record=self.journal.record(owner.token)['host_network_policy']
        self.assertIsNone(record['pending']);self.assertEqual(self.journal.snapshot()['schema'],17)
        self.reopen();self.command.reset_mock()
        restored=self.restore();restored.verify()
        self.command.run.assert_not_called()
        self.assertEqual((self.directory/owner.name).stat().st_ino,before)
        restored.close()
        self.assertNotIn('host_network_policy',self.journal.record(owner.token))
        self.assertFalse(tuple(self.directory.iterdir()));self.command.run.assert_called_once()

    def test_acknowledged_stage_recovers_before_rename(self):
        with patch('karyo_profiles.host_network_policy.os.rename',side_effect=RuntimeError('publication pause')):
            with self.assertRaisesRegex(RuntimeError,'publication pause'):HostNetworkPolicy.start(self.journal,self.dns)
        value=self.journal.record(self.dns.token)['host_network_policy']
        self.assertEqual(value['pending'],'publish');self.assertIsNotNone(value['file'])
        self.reopen();self.restore().close()
        self.assertFalse(tuple(self.directory.iterdir()))
        self.assertNotIn('host_network_policy',self.journal.record(self.dns.token))

    def test_published_rule_recovers_after_reload_failure(self):
        self.command.run.side_effect=TimeoutError('reload pause')
        with self.assertRaisesRegex(TimeoutError,'reload pause'):HostNetworkPolicy.start(self.journal,self.dns)
        self.assertEqual(self.journal.record(self.dns.token)['host_network_policy']['pending'],'reload')
        self.command.run.side_effect=None
        self.reopen();self.restore().close()
        self.assertFalse(tuple(self.directory.iterdir()))

    def test_removal_recovers_after_unlink_before_reload_acknowledgement(self):
        owner=HostNetworkPolicy.start(self.journal,self.dns);self.addCleanup(owner.release_handles)
        self.command.run.side_effect=TimeoutError('remove pause')
        with self.assertRaisesRegex(TimeoutError,'remove pause'):owner.close()
        self.assertEqual(self.journal.record(self.dns.token)['host_network_policy']['pending'],'remove')
        self.assertFalse(tuple(self.directory.iterdir()))
        self.command.run.side_effect=None
        self.reopen();self.restore().close()
        self.assertNotIn('host_network_policy',self.journal.record(self.dns.token))

    def test_neighbor_policy_keeps_its_exact_inode_and_contents(self):
        first=HostNetworkPolicy.start(self.journal,self.dns);self.addCleanup(first.release_handles)
        other_dns=self.context(WORK)
        second=HostNetworkPolicy.start(self.journal,other_dns);self.addCleanup(second.release_handles)
        path=self.directory/second.name;before=(path.stat().st_ino,path.read_bytes())
        first.close();second.verify()
        self.assertEqual((path.stat().st_ino,path.read_bytes()),before)
        self.reopen();self.restore(other_dns).close()
        self.assertFalse(tuple(self.directory.iterdir()))
