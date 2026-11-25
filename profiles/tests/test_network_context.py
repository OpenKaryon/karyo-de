# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary session routing hints, independent of worker availability."""
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_profiles.network_client import CONTEXT_KEY,session_context
from karyo_profiles.networks import NetworkError

ENDPOINT='/run/karyo-network-manager-'+ 'ab'*16 +'/client.json'


class SessionContextTest(unittest.TestCase):
    def test_child_uses_its_inherited_hint_without_contacting_the_manager(self):
        query=Mock(side_effect=NetworkError('manager is restarting'))
        self.assertEqual(session_context({CONTEXT_KEY:ENDPOINT},query=query),ENDPOINT)
        query.assert_not_called()

    def test_main_reads_only_the_context_from_the_native_manager(self):
        query=Mock(return_value=['EDITOR=existing-editor','THEME=light',CONTEXT_KEY+'='+ENDPOINT])
        environment={'EDITOR':'my-editor'}
        self.assertEqual(session_context(environment,query=query),ENDPOINT)
        self.assertEqual(environment,{'EDITOR':'my-editor'})
        query.assert_called_once_with()

    def test_unmanaged_session_needs_no_network_service(self):
        self.assertEqual(session_context({},query=lambda:['EDITOR=existing-editor']),'')

    def test_native_manager_unavailable_does_not_stop_an_unmanaged_desktop(self):
        self.assertEqual(session_context({},query=Mock(side_effect=NetworkError('not running'))),'')
