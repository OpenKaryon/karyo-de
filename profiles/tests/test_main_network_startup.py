# SPDX-License-Identifier: GPL-3.0-or-later
"""Main entry uses the same optional pre-login provider as child desktops."""
import contextlib
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles import main_dispatcher as main
from karyo_profiles.model import DesktopSettings
from karyo_profiles.netbird_client import session_context


class MainNetworkStartupTest(unittest.TestCase):
    def entry(self, vpn):
        registry = SimpleNamespace(journal=Mock(), store=Mock())
        registry.store.settings.return_value = DesktopSettings(vpn=vpn)
        accounts, sessions, account, peer = Mock(), Mock(), Mock(name='account'), Mock()
        account.name = 'home'
        events, owners = [], {}
        stack = contextlib.ExitStack();self.addCleanup(stack.close)
        for name in ('enabled', 'forwarding_ready', 'supported_resolver_layout', 'program_identity',
                     'policy_loader_ready', 'discover_underlay', 'discover_dns', 'netbird_ready'):
            stack.enter_context(patch.object(main, name, return_value=True))
        def owner(kind):
            result = Mock();owners[kind] = result
            if kind == 'JournalNetwork':result.network.fd = None
            def start(*args, **kwargs):
                events.append(kind);return result
            target = stack.enter_context(patch.object(main, kind))
            getattr(target, 'prepare_main' if kind == 'JournalNetwork' else 'prepare' if kind == 'MainEntry' else 'start').side_effect = start
        for name in ('JournalNetwork', 'JournalForwarding', 'JournalDNS', 'JournalNetworkService',
                     'HostNetworkPolicy', 'JournalNetBird', 'MainEntry'):owner(name)
        main.prepare_entry(registry, account, peer, accounts, sessions)
        return registry, events, owners

    def test_plain_home_needs_no_optional_vendor_and_keeps_its_private_base(self):
        registry, events, owners = self.entry('none')
        self.assertEqual(events, ['JournalNetwork', 'JournalForwarding', 'JournalDNS',
                                 'JournalNetworkService', 'HostNetworkPolicy', 'MainEntry'])
        main.netbird_ready.assert_not_called()
        owners['JournalNetBird'].release_handles.assert_not_called()
        owners['JournalNetworkService'].release_handles.assert_called_once()
        registry.store.settings.assert_called_once_with('home')

    def test_optional_home_provider_is_preflighted_started_and_handed_off_before_pam(self):
        _, events, owners = self.entry('netbird')
        main.netbird_ready.assert_called_once()
        self.assertEqual(events[-3:], ['HostNetworkPolicy', 'JournalNetBird', 'MainEntry'])
        owners['JournalNetBird'].release_handles.assert_called_once()
        owners['JournalNetBird'].close.assert_not_called()

    def test_main_recovers_only_its_provider_hint_for_the_graphical_catalog(self):
        path = '/run/karyo-netbird-'+'ab'*16+'/client.json'
        query = Mock(return_value=['EDITOR=existing-editor', 'KARYO_NETBIRD_ENDPOINT='+path])
        self.assertEqual(session_context({}, query=query), path)
        self.assertEqual(session_context({'KARYO_NETBIRD_ENDPOINT':path}, query=query), path)
        query.assert_called_once()

    def test_plain_main_needs_no_provider_hint(self):
        self.assertEqual(session_context({}, query=lambda:['EDITOR=existing-editor']), '')
