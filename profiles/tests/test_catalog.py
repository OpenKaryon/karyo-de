# SPDX-License-Identifier: GPL-3.0-or-later
from copy import deepcopy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.catalog import DATA_ENVIRONMENT, NETBIRD_DESKTOP, require_netbird_catalog
from karyo_profiles.journal import validate_record
from karyo_profiles.journal_session import JournalSession, manager_receipt, restore_manager
from karyo_profiles.journal_session_cleanup import removing_manager
from karyo_profiles.journal_session_record import validate_session
from karyo_profiles.user_manager import ManagerRetained, UserManagerScope
import test_graphical as graphical
import test_netbird as provider_fixture
import test_user_manager as manager_fixture
from test_journal_netbird import provider, record
from test_journal_session import ACCOUNT, prepared


def manager(catalog=False):
    files = provider_fixture.files(); files.plan.client_properties.return_value = ('NetworkNamespacePath='+files.network.path,)
    return UserManagerScope(files, netbird=True, catalog=catalog)


class CatalogPolicyTest(unittest.TestCase):
    def test_catalog_reaches_manager_and_graphical_owner_only_when_selected(self):
        legacy, current = manager(), manager(True)
        self.assertNotIn(b'XDG_DATA_DIRS', legacy.contents)
        self.assertEqual(current.environment, legacy.environment+(DATA_ENVIRONMENT,))
        self.assertIn(DATA_ENVIRONMENT.encode(), current.contents)
        fixture = graphical.GraphicalTest(); fixture.setUp(); self.addCleanup(fixture.doCleanups)
        fixture.lease.manager.environment = current.environment; fixture.start()
        launch = next(row for row in fixture.kernel.calls if row[0].endswith('systemd-run'))
        self.assertIn('--setenv='+DATA_ENVIRONMENT, launch)
        plain = UserManagerScope(current.files)
        self.assertEqual(plain.environment, ())
        for options in ({'catalog': True}, {'netbird': True, 'catalog': 'netbird'}):
            with self.assertRaises(ValueError): UserManagerScope(current.files, **options)

    def test_only_fixed_catalog_kind_and_captured_provider_are_accepted(self):
        value = {'pending': None, 'manager': prepared(), 'graphical': None, 'catalog': 'netbird'}
        validate_session(value, ACCOUNT)
        for kind in (None, True, [], ['netbird'], '/home/work/apps', 'other'):
            with self.assertRaises(ValueError): validate_session(dict(value, catalog=kind), ACCOUNT)
        proof = dict(record(), session=value)
        with self.assertRaises(ValueError): validate_record(proof)
        validate_record(dict(proof, provider=provider()))

    def test_catalog_is_published_before_manager_preparation(self):
        journal, dns, scope = Mock(), Mock(), Mock()
        before = {'dns': {'pending': None}, 'provider': {'kind': 'netbird'}}
        journal.record.return_value = before; dns.network.network.fd = None
        scope.fd = scope.pidfd = None
        def preparing():
            after = journal.change.call_args.args[1]
            self.assertEqual(after['session'], {'pending': 'manager-prepare', 'manager': None, 'graphical': None, 'catalog': 'netbird'})
            raise RuntimeError('Stop after captured intent')
        scope.prepare.side_effect = preparing
        with patch('karyo_profiles.journal_session.UserManagerScope', return_value=scope) as factory, \
                patch('karyo_profiles.journal_session.restore_optional'):
            with self.assertRaisesRegex(RuntimeError, 'Stop after captured intent'): JournalSession.start(journal, dns)
        factory.assert_called_once_with(dns.files, netbird=True, catalog=True)


@unittest.skipUnless(os.geteuid() == 0, 'root-owned catalog and policy compatibility')
class CatalogResourcesTest(unittest.TestCase):
    def test_missing_writable_symlink_or_invalid_resource_never_becomes_a_catalog(self):
        with tempfile.TemporaryDirectory(prefix='karyo-catalog-', dir='/root') as temp:
            root = Path(temp); apps = root/'applications'; apps.mkdir(); path = apps/NETBIRD_DESKTOP
            with self.assertRaises(FileNotFoundError): require_netbird_catalog(root)
            path.write_bytes(b'[Desktop Entry]\nName=NetBird\n'); require_netbird_catalog(root)
            path.chmod(0o666)
            with self.assertRaises(ValueError): require_netbird_catalog(root)
            path.chmod(0o644); path.write_bytes(b'invalid')
            with self.assertRaises(ValueError): require_netbird_catalog(root)
            path.unlink(); path.symlink_to(root/'other')
            with self.assertRaises(OSError): require_netbird_catalog(root)
            path.unlink(); path.write_bytes(b'[Desktop Entry]\n'); apps.chmod(0o775)
            with self.assertRaises(ValueError): require_netbird_catalog(root)


@unittest.skipUnless(os.geteuid() == 0, 'root-owned manager policy files')
class PolicyCompatibilityTest(unittest.TestCase):
    def setUp(self):
        self.fixture = manager_fixture.ManagerScopeTest(); self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.scope.network.spec.token = 'ab'*16

    def publish(self, catalog):
        fixture = self.fixture; original = fixture.scope
        scope = UserManagerScope(original.files, sessions=fixture.sessions, netbird=True, catalog=catalog)
        scope.directory = original.directory; fixture.scope = scope
        fixture.kernel = manager_fixture.Kernel(scope); scope.command = scope.process = fixture.kernel
        scope.network.command = fixture.kernel
        with patch('karyo_profiles.user_manager.require_netbird_catalog') as require:
            scope.prepare()
            self.assertEqual(require.call_count, int(catalog))
        return scope, manager_receipt(scope)

    def factory(self, files, **options):
        scope = UserManagerScope(files, sessions=self.fixture.sessions, **options)
        scope.directory = self.fixture.scope.directory
        scope.command = scope.process = self.fixture.kernel
        return scope

    def check_variant(self, selected):
        original, proof = self.publish(selected)
        with patch('karyo_profiles.journal_session.UserManagerScope', side_effect=self.factory):
            restored = restore_manager(original.files, proof, netbird=True, catalog=selected)
        try:
            self.assertEqual(restored.contents, original.contents)
            self.assertEqual(restored.catalog, selected)
        finally: os.close(restored.fd); restored.fd = None
        with patch('karyo_profiles.journal_session_cleanup.UserManagerScope', side_effect=self.factory):
            closing = removing_manager(original.files, proof, allow_missing=False, netbird=True, catalog=selected)
        try: self.assertEqual(closing.contents, original.contents)
        finally: os.close(closing.fd); closing.fd = None
        before = (original.directory/original.filename).read_bytes(); count = len(self.fixture.kernel.calls)
        with patch('karyo_profiles.journal_session.UserManagerScope', side_effect=self.factory):
            with self.assertRaises(ManagerRetained): restore_manager(original.files, proof, netbird=True, catalog=not selected)
        self.assertEqual((original.directory/original.filename).read_bytes(), before)
        self.assertFalse(any(row[-1] == 'daemon-reload' for row in self.fixture.kernel.calls[count:]))

    def test_legacy_policy_restores_and_closes_without_adding_catalog(self): self.check_variant(False)
    def test_catalog_policy_restores_and_closes_without_rewriting_legacy_policy(self): self.check_variant(True)


@unittest.skipUnless(os.geteuid() == 0, 'root journal promotion')
class CatalogJournalTest(unittest.TestCase):
    def test_schema7_retains_explicit_catalog_and_never_downgrades_after_session_removal(self):
        from test_journal import JournalFixture, WORK, SOURCE
        fixture = JournalFixture(); fixture.addCleanup = self.addCleanup; fixture.setUp()
        journal = fixture.journal; token = journal.begin(WORK, SOURCE); before = journal.record(token)
        value = dict(record(token), provider=provider(), session={'pending': None, 'manager': prepared(), 'graphical': None, 'catalog': 'netbird'})
        value['network']['contents'] = 'tunnel'
        journal.change(before, value); self.assertEqual(journal.snapshot()['schema'], 7)
        downgraded = journal.snapshot(); downgraded['schema'] = 6
        with self.assertRaises(ValueError): journal.save(downgraded)
        fixture.reopen(); journal = fixture.journal
        self.assertEqual(journal.record(token)['session']['catalog'], 'netbird')
        before = journal.record(token); after = deepcopy(before); after.pop('session')
        journal.change(before, after); self.assertEqual(journal.snapshot()['schema'], 7)
        before = journal.record(token); after = deepcopy(before); after.pop('provider')
        journal.change(before, after); self.assertEqual(journal.snapshot()['schema'], 7)
