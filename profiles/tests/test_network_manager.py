# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary private service plans and versioned resolver configuration."""
from dataclasses import replace
from pathlib import Path
import unittest
from unittest.mock import Mock, patch
import xml.etree.ElementTree as ET

from test_dns import SEED, SPEC
from test_netbird import files
from karyo_profiles.dns import ResolverPlan
from karyo_profiles.network_manager import CONFIGURATION, NetworkManagerScope
from karyo_profiles.services import OwnedUnit


class NetworkManagerPlanTest(unittest.TestCase):
    def scope(self, netplan):
        value = files()
        value.network.store.runtime = Path('/run/karyo')
        value.plan.service_view = 'network-manager'
        value.plan.bus_name = 'karyo-resolve-bus-' + value.network.spec.token
        with patch('karyo_profiles.network_manager.Path.is_file', return_value=netplan):
            return NetworkManagerScope(Mock(files=value))

    def test_native_keyfile_paths_are_local_to_the_instance(self):
        scope = self.scope(False)
        values = dict(row.split('=', 1) for row in scope.properties())
        self.assertEqual(scope.unit.program, '/usr/sbin/NetworkManager')
        self.assertFalse(scope.clients)  # Older explicit workers keep their original receipt semantics.
        self.assertEqual(scope.state.path, Path('/var/lib/karyo/profile-vpn/1002/network-manager'))
        self.assertIn(str(scope.state.path) + '/etc:/etc/NetworkManager', values['BindPaths'])
        self.assertIn(str(scope.directory) + ':/run/NetworkManager', values['BindPaths'])
        self.assertEqual(values['NetworkNamespacePath'], scope.network.path)
        self.assertEqual(values['Environment'], 'DBUS_SYSTEM_BUS_ADDRESS=' + scope.files.plan.bus_address)
        self.assertIn('/run/' + scope.files.plan.bus_name + '/bus:/run/dbus/system_bus_socket',
                      values['BindReadOnlyPaths'])
        self.assertIn('CAP_KILL', values['CapabilityBoundingSet'])
        self.assertIn(b'[device-karyo-uplink]\nmatch-device=interface-name:eth0\nmanaged=1\n', CONFIGURATION)
        self.assertNotIn('ExecStartPre', values)
        OwnedUnit.properties(scope.properties())

    def test_stock_netplan_generator_uses_private_inputs_and_outputs(self):
        scope = self.scope(True)
        values = dict(row.split('=', 1) for row in scope.properties())
        for name, target in (('etc', '/etc/netplan'), ('run', '/run/netplan'),
                             ('lib', '/usr/lib/netplan'), ('udev', '/run/udev/rules.d'),
                             ('networkd', '/run/systemd/network')):
            self.assertIn(str(scope.state.path) + '/netplan/' + name + ':' + target, values['BindPaths'])
        self.assertEqual(values['ExecStartPre'], '/usr/libexec/netplan/configure --networkmanager-only')
        OwnedUnit.properties(scope.properties())

    def test_old_resolver_view_remains_the_default(self):
        old = ResolverPlan(SPEC, SEED, 1001, 990, '/run/karyo/test')
        self.assertEqual(old.files(), replace(old, service_view='resolver').files())
        self.assertNotIn(b'NetworkManager', old.files()['bus.conf'])
        current = replace(old, service_view='network-manager')
        self.assertEqual(current.files()['resolved.conf'], old.files()['resolved.conf'])
        root = ET.fromstring(current.files()['bus.conf'])
        self.assertIsNotNone(root.find('./policy[@user="root"]/allow[@own_prefix="org.freedesktop.NetworkManager"]'))
        self.assertIsNotNone(root.find('./policy[@user="1001"]/allow[@send_destination="org.freedesktop.NetworkManager"]'))
