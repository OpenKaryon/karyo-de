# SPDX-License-Identifier: GPL-3.0-or-later
"""Physical inventory, ordinary connection plans and activation completion."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.physical_backend import PhysicalBackend, NM, ROOT, DEVICE, WIRELESS, ACTIVE, SETTINGS
from karyo_profiles.physical_caller import PhysicalCaller
from karyo_profiles.logind import Session

WIRED = '11111111-1111-4111-8111-111111111111'
WIFI = '22222222-2222-4222-8222-222222222222'


class API:
    def __init__(self):
        self.owner = ':1.30'
        self.renew = Mock();self.verify = Mock();self.activate = Mock(return_value='/active/2')
        self.connect_wifi = Mock(return_value='/active/2');self.set_radio = Mock()
        self.call_log = []
        self.maps = {(ROOT, NM): {'WirelessEnabled': True, 'ActiveConnections': ['/active/1']},
            ('/device/1', DEVICE): {'DeviceType': 1, 'Interface': 'eth0', 'Managed': True, 'State': 100},
            ('/device/2', DEVICE): {'DeviceType': 2, 'Interface': 'wlan0', 'Managed': True, 'State': 30},
            ('/device/3', DEVICE): {'DeviceType': 29, 'Interface': 'wg0', 'Managed': True, 'State': 100},
            ('/active/1', ACTIVE): {'Type': '802-3-ethernet', 'Uuid': WIRED, 'Id': 'Cable', 'Devices': ['/device/1'], 'State': 2},
            ('/active/2', ACTIVE): {'State': 1},
            (ROOT+'/AccessPoint/1', NM+'.AccessPoint'): {'Ssid': b'Lab Wi-Fi', 'Flags': 1, 'WpaFlags': 0,
                'RsnFlags': 0x100, 'Strength': 75}}
        self.saved = {'/profile/1': {'connection': {'type': '802-3-ethernet', 'id': 'Cable', 'uuid': WIRED}},
            '/profile/2': {'connection': {'type': '802-11-wireless', 'id': 'Wireless', 'uuid': WIFI}},
            '/profile/3': {'connection': {'type': 'wireguard', 'id': 'Host VPN', 'uuid': '33'*16}}}

    def properties(self, path, interface):return deepcopy(self.maps[path, interface])

    def call(self, path, interface, method, *args):
        self.call_log.append((path, interface, method, args))
        if method == 'GetDevices':return ['/device/1', '/device/2', '/device/3']
        if method == 'ListConnections':return list(self.saved)
        if method == 'GetSettings':return deepcopy(self.saved[path])
        if method == 'GetAllAccessPoints':return [ROOT+'/AccessPoint/1']
        if method == 'Disconnect':return None
        raise AssertionError(method)


class PhysicalNetworkTest(unittest.TestCase):
    def setUp(self):
        self.api = API()
        self.backend = PhysicalBackend(self.api, kernel=lambda name: (1 if name == 'eth0' else 2, 3, 4))

    def test_hardware_inventory_has_physical_profiles_and_wifi_without_host_vpn(self):
        value = self.backend.inventory()
        self.assertTrue(value['connected']);self.assertEqual(value['name'], 'Cable')
        self.assertEqual([p['uuid'] for p in value['connections']], [WIRED, WIFI])
        self.assertEqual(value['access_points'][0]['security'], 'WPA2')
        self.assertEqual(value['access_points'][0]['ssid'], 'Lab Wi-Fi')

    def test_existing_non_nm_ethernet_is_reported_without_taking_over_its_configuration(self):
        self.api.maps['/device/1', DEVICE]['Managed'] = False
        self.api.maps[ROOT, NM]['ActiveConnections'] = []
        self.backend.external = lambda interface: interface == 'eth0'
        value = self.backend.inventory()
        self.assertTrue(value['connected'])
        external = next(p for p in value['connections'] if p.get('external'))
        self.assertEqual(external['interface'], 'eth0')
        self.assertEqual(external['uuid'], '')
        self.api.activate.assert_not_called()

    def test_wireless_plan_uses_current_access_point_and_normal_nm_authorization_actions(self):
        plan = self.backend.prepare({'version': 1, 'owner': self.api.owner, 'operation': 'wifi', 'interface': 'wlan0',
            'access_point': ROOT+'/AccessPoint/1', 'password': 'test-passphrase'})
        self.assertEqual(plan.actions, ('network-control', 'settings.modify.system'))
        self.api.maps[ROOT+'/AccessPoint/1', NM+'.AccessPoint']['Strength'] = 60
        self.assertEqual(self.backend.execute(plan), '/active/2')
        self.assertEqual(self.api.connect_wifi.call_args.args[0], '/device/2')

    def test_saved_ethernet_activation_and_disconnect_use_its_physical_device(self):
        plan = self.backend.prepare({'version': 1, 'owner': self.api.owner, 'operation': 'activate', 'uuid': WIRED})
        self.assertEqual(plan.actions, ('network-control',))
        self.backend.execute(plan)
        self.api.activate.assert_called_once_with('/profile/1', '/device/1')
        self.backend.execute(self.backend.prepare({'version': 1, 'owner': self.api.owner, 'operation': 'disconnect', 'uuid': WIRED}))
        self.assertIn(('/device/1', DEVICE, 'Disconnect', ()), self.api.call_log)

    def test_radio_and_activation_report_normal_progress_and_password_failure(self):
        plan = self.backend.prepare({'version': 1, 'owner': self.api.owner, 'operation': 'radio', 'enabled': False})
        self.assertEqual(plan.actions, ('enable-disable-wifi',))
        self.backend.execute(plan);self.api.set_radio.assert_called_once_with(False)
        wifi = self.backend.prepare({'version': 1, 'owner': self.api.owner, 'operation': 'wifi', 'interface': 'wlan0',
            'access_point': ROOT+'/AccessPoint/1', 'password': 'test-passphrase'})
        self.assertEqual(self.backend.activation(wifi, '/active/2'), (False, ''))
        self.api.maps['/active/2', ACTIVE]['State'] = 2
        self.assertEqual(self.backend.activation(wifi, '/active/2'), (True, ''))
        self.api.maps['/active/2', ACTIVE]['State'] = 4
        done, message = self.backend.activation(wifi, '/active/2')
        self.assertTrue(done);self.assertIn('authentication failed', message)

    def test_caller_is_original_unique_bus_peer_and_active_uid_even_for_user_manager_apps(self):
        bus = Mock();bus.get_unix_user.return_value = 1001
        bus.get_object.return_value.get_dbus_method.return_value.return_value = 4321
        sessions = Mock();sessions.all.return_value = [Session('12', 'home', 1001, 13, True)]
        caller = PhysicalCaller(bus, ':1.44', sessions=sessions, start=lambda pid: 100)
        self.assertEqual((caller.uid, caller.pid, caller.sender), (1001, 4321, ':1.44'))
        caller.verify()
