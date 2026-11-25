# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded calls pinned to the original host NetworkManager bus owner."""
import time
import uuid

from .physical_backend import NM, ROOT


class NetworkManagerAPI:
    def __init__(self, bus):
        import dbus
        self.dbus, self.bus = dbus, bus
        self.owner = str(bus.get_name_owner(NM))
        self.renew()

    def renew(self):self.deadline = time.monotonic() + 5

    def verify(self):
        if str(self.bus.get_name_owner(NM)) != self.owner:
            raise RuntimeError('The host network manager restarted')

    def call(self, path, interface, method, *args, signature=None):
        remaining = min(2, self.deadline - time.monotonic())
        if remaining <= 0:raise TimeoutError('Physical network query timed out')
        obj = self.bus.get_object(self.owner, str(path), introspect=False)
        extra = {} if signature is None else {'signature': signature}
        return obj.get_dbus_method(method, interface)(*args, timeout=remaining, **extra)

    def properties(self, path, interface):
        return self.call(path, 'org.freedesktop.DBus.Properties', 'GetAll', interface)

    def set_radio(self, enabled):
        self.call(ROOT, 'org.freedesktop.DBus.Properties', 'Set', NM, 'WirelessEnabled',
                  self.dbus.Boolean(enabled, variant_level=1), signature='ssv')

    def activate(self, profile, device):
        d = self.dbus
        return self.call(ROOT, NM, 'ActivateConnection', d.ObjectPath(profile), d.ObjectPath(device), d.ObjectPath('/'))

    def connect_wifi(self, device, point, password):
        d = self.dbus
        settings = {'connection': {'id': d.String(point['ssid'].decode('utf-8', 'replace')),
                    'uuid': d.String(str(uuid.uuid4())), 'type': d.String('802-11-wireless')},
                    '802-11-wireless': {'ssid': d.ByteArray(point['ssid'])},
                    'ipv4': {'method': d.String('auto')}, 'ipv6': {'method': d.String('auto')}}
        kind = point['security']
        if kind != '--':
            values = {'key-mgmt': d.String('owe' if kind == 'OWE' else 'sae' if kind == 'WPA3' else 'wpa-psk')}
            if kind != 'OWE':values['psk'] = d.String(password)
            settings['802-11-wireless-security'] = values
        values = d.Dictionary({k: d.Dictionary(v, signature='sv') for k, v in settings.items()}, signature='sa{sv}')
        profile, active = self.call(ROOT, NM, 'AddAndActivateConnection', values,
                                    d.ObjectPath(device), d.ObjectPath(point['path']))
        return active
