# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only owner models with real kernel descriptor pinning, not login QA."""
from copy import deepcopy
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.dispatcher import HostDispatcher, receive, send
from karyo_profiles.entrypoints import network_info
from karyo_profiles.logind import Session
from karyo_profiles.model import Ownership
from karyo_profiles.network_info import describe
from karyo_profiles.networks import NetworkCommand, NetworkError, NetworkSpec
from karyo_profiles.switching import DesktopStopped
from karyo_profiles.underlay import PhysicalUnderlay

HOME = LocalAccount('home', 1001, 1001, '/home/home', '/bin/sh')
WORK = LocalAccount('work', 1002, 1002, '/home/work', '/bin/sh')
OTHER = LocalAccount('other', 1003, 1003, '/home/other', '/bin/sh')


@unittest.skipUnless(os.geteuid() == 0, 'host owner inspection fixture')
class OwnerInfoTests(unittest.TestCase):
    def setUp(self):
        # No namespace or login is created. Registered owners are explicit models.
        self.fd = os.open('/proc/self/ns/net', os.O_RDONLY | os.O_CLOEXEC)
        self.addCleanup(os.close, self.fd)
        info = os.fstat(self.fd)
        self.namespace = (info.st_dev, info.st_ino)
        self.directory = tempfile.TemporaryDirectory(dir='/root')
        self.addCleanup(self.directory.cleanup)
        runtime = Path(self.directory.name)/'resolver'
        runtime.mkdir(mode=0o750)
        descriptor = os.stat(runtime)
        self.runtime_identity = [descriptor.st_dev, descriptor.st_ino]
        self.record = {'pending': None, 'session': {'pending': None},
            'forwarding': {'pending': None}, 'dns': {'pending': None,
            'resolver': {'runtime': [self.runtime_identity]}}}
        self.peer = SimpleNamespace(verify=Mock(return_value=HOME),
            identity=SimpleNamespace(pid=os.getpid(), session=('c1', HOME.uid, HOME.name, 8)))
        self.store = SimpleNamespace(_locked=True,
            ownership=lambda: Ownership.parse('home work\nother\n'))
        self.accounts = SimpleNamespace(by_uid=lambda uid: {1001: HOME, 1002: WORK, 1003: OTHER}[uid])
        self.scope = self.scope_for(WORK, runtime)
        self.unrelated = self.scope_for(OTHER, runtime, token='cd' * 16)
        self.registry = SimpleNamespace(store=self.store,
            profiles={self.scope.token: self.scope, self.unrelated.token: self.unrelated})

    def scope_for(self, account, runtime, token='ab' * 16):
        network = SimpleNamespace(fd=self.fd, identity=self.namespace,
            indices=(7, 8), spec=NetworkSpec(account.uid, token), command=NetworkCommand())
        return SimpleNamespace(token=token, manager=SimpleNamespace(account=account),
            network=SimpleNamespace(network=network), verify_consumers=Mock(),
            journal=SimpleNamespace(record=lambda token: deepcopy(self.record)),
            graph=SimpleNamespace(closed=False, identity=SimpleNamespace(login=Session(
                'c2', account.name, account.uid, 9, False))),
            dns=SimpleNamespace(files=SimpleNamespace(plan=SimpleNamespace(
                runtime_paths=(str(runtime),), service_uid=os.geteuid(), gid=os.getegid(),
                bus_address='unix:path=/run/karyo-model-bus/bus')),
                services=SimpleNamespace(resolver=SimpleNamespace(identity=SimpleNamespace(
                    pid=345, start=678, invocation='ef' * 16)))),
            forwarding=SimpleNamespace(gateway=SimpleNamespace(physical=PhysicalUnderlay(
                'eth0', 2, '192.0.2.2', 24, '192.0.2.1', 100))))

    def capture(self):
        return describe(self.peer, self.registry, self.store, self.accounts)

    def test_home_inspects_only_its_family_and_exports_no_mutation_authority(self):
        result = self.capture()
        self.assertEqual(result['kind'], 'host')
        self.assertTrue(result['inspection_only'])
        self.assertIsNone(result['resolver'])
        self.assertEqual([row['uid'] for row in result['physical_peers']], [WORK.uid])
        self.unrelated.verify_consumers.assert_not_called()
        self.assertEqual(result['namespace'], list(self.namespace))
        json.dumps(result)

    def test_current_child_inspects_the_existing_resolver_and_namespace(self):
        self.peer.verify.return_value = WORK
        self.peer.identity.session = ('c2', WORK.uid, WORK.name, 9)
        result = self.capture()
        self.assertEqual(result['kind'], 'profile')
        self.assertEqual(result['profile_token'], self.scope.token)
        self.assertEqual(result['resolver']['directory'], self.runtime_identity)
        self.assertEqual(result['resolver']['namespace'], list(self.namespace))
        self.assertEqual(result['resolver']['pid'], 345)

    def test_repeated_inspection_closes_all_temporary_descriptors(self):
        before = len(os.listdir('/proc/self/fd'))
        for unused in range(8):
            self.capture()
        self.assertEqual(len(os.listdir('/proc/self/fd')), before)
        self.assertFalse(os.get_inheritable(self.fd))

    def test_normal_carrier_transition_is_reported_as_not_ready(self):
        self.record['forwarding']['refresh'] = {'step': 2}
        with self.assertRaisesRegex(NetworkError, 'transition'):
            self.capture()

    def test_exited_profile_is_not_returned_from_an_older_inventory(self):
        def finish():
            self.registry.profiles.pop(self.scope.token, None)
        self.peer.verify.side_effect = (HOME, HOME)
        self.scope.verify_consumers.side_effect = finish
        with self.assertRaisesRegex(NetworkError, 'lifetime'):
            self.capture()

    def test_empty_family_uses_the_actual_host_namespace(self):
        self.registry.profiles = {}
        result = self.capture()
        self.assertEqual(result['physical_peers'], [])
        self.assertEqual(result['namespace'], list(self.namespace))


class InspectionTransportTests(unittest.TestCase):
    def test_fixed_operation_reaches_the_captured_peer_once(self):
        from contextlib import nullcontext
        store = SimpleNamespace(locked=nullcontext)
        caller = SimpleNamespace(verify=Mock(return_value=HOME), close=Mock())
        callback = Mock(return_value={'inspection_only': True, 'uid': HOME.uid})
        dispatcher = HostDispatcher(store, peer=lambda *args, **kwargs: caller,
            guard=lambda: None, network_info=callback)
        client, server = socket.socketpair()
        try:
            send(client, {'version': 1, 'operation': 'network-info'}, 4096, 1)
            dispatcher.handle(server)
            response = receive(client, 65536, 1)
            self.assertEqual(response['result'], {'inspection_only': True, 'uid': HOME.uid})
            callback.assert_called_once_with(caller)
            caller.close.assert_called_once()
        finally:
            client.close()
            server.close()

    def test_helper_uses_existing_authorization_and_fixed_host_request(self):
        authorize = Mock(return_value=WORK)
        request = Mock(return_value={'inspection_only': True})
        result = network_info([], environment={'PKEXEC_UID': str(WORK.uid)},
            authorize=authorize, runtime=lambda: True, request=request)
        self.assertTrue(result['inspection_only'])
        request.assert_called_once_with({'version': 1, 'operation': 'network-info'})
        authorize.assert_called_once()

    def test_disabled_optional_runtime_is_not_started_by_inspection(self):
        request = Mock()
        with self.assertRaises(DesktopStopped):
            network_info([], environment={}, authorize=Mock(return_value=HOME),
                runtime=lambda: False, request=request)
        request.assert_not_called()


if __name__ == '__main__':
    unittest.main()
