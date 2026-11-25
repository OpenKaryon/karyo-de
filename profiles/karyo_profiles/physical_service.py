# SPDX-License-Identifier: GPL-3.0-or-later
"""Narrow, original-caller physical network mediator on the host system bus."""
import json
import os
from pathlib import Path
import time

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

from .physical_api import NetworkManagerAPI
from .physical_backend import PhysicalBackend, packet
from .physical_caller import PhysicalCaller

NAME = 'org.karyo.PhysicalNetwork'
PATH = '/org/karyo/PhysicalNetwork'
INTERFACE = NAME + '1'


class OperationError(dbus.DBusException):
    _dbus_error_name = INTERFACE + '.Error'


class PhysicalNetwork(dbus.service.Object):
    def __init__(self, bus):
        self.bus = bus
        self.pending = False
        self.name = dbus.service.BusName(NAME, bus=bus, do_not_queue=True)
        super().__init__(self.name, PATH)

    @dbus.service.method(INTERFACE, in_signature='', out_signature='s', sender_keyword='sender')
    def GetState(self, sender=None):
        try:
            caller = PhysicalCaller(self.bus, sender)
            value = PhysicalBackend(NetworkManagerAPI(self.bus)).inventory()
            caller.verify()
            return json.dumps(value, ensure_ascii=True, allow_nan=False)
        except Exception:
            raise OperationError('Physical network information is unavailable.') from None

    @dbus.service.method(INTERFACE, in_signature='s', out_signature='s', sender_keyword='sender',
                         async_callbacks=('reply', 'error'))
    def Change(self, text, reply, error, sender=None):
        def failed(message):
            self.pending = False
            error(OperationError(message))
        try:
            if self.pending:raise RuntimeError('Another network change is in progress.')
            if len(text.encode('utf-8')) > 4096:raise ValueError('Request too large')
            caller = PhysicalCaller(self.bus, sender)
            request = packet(json.loads(text))
            backend = PhysicalBackend(NetworkManagerAPI(self.bus))
            plan = backend.prepare(request)
        except Exception:
            error(OperationError('This physical network change is unavailable.'))
            return
        self.pending = True
        deadline = time.monotonic() + 90
        actions = list(plan.actions)

        def complete():
            self.pending = False
            reply(json.dumps({'version': 1, 'success': True}))

        def activated(path, started):
            try:
                caller.verify()
                done, message = backend.activation(plan, path)
                if done:
                    failed(message) if message else complete()
                    return False
                if time.monotonic() - started >= 45:
                    failed('The network did not respond. Try again.')
                    return False
            except Exception:
                failed('The network connection could not finish. Try again.')
                return False
            return True

        def perform():
            try:
                caller.verify()
                path = backend.execute(plan)
                if path is None:complete()
                else:GLib.timeout_add(250, activated, path, time.monotonic())
            except Exception:
                failed('The physical network change could not finish.')

        def authorize():
            try:
                caller.verify()
                if time.monotonic() >= deadline:raise TimeoutError()
                if not actions:
                    perform()
                    return
                action = 'org.freedesktop.NetworkManager.' + actions.pop(0)
                authority = dbus.Interface(self.bus.get_object('org.freedesktop.PolicyKit1',
                    '/org/freedesktop/PolicyKit1/Authority'), 'org.freedesktop.PolicyKit1.Authority')
                subject = dbus.Struct(('system-bus-name', dbus.Dictionary({'name': dbus.String(sender)}, signature='sv')),
                                      signature='sa{sv}')
                def checked(result):
                    if bool(result[0]):authorize()
                    else:failed('The network change was not authorized.')
                authority.CheckAuthorization(subject, action, dbus.Dictionary({}, signature='ss'),
                    dbus.UInt32(1), '', reply_handler=checked,
                    error_handler=lambda unused: failed('Network authorization is unavailable.'), timeout=90)
            except Exception:
                failed('The active network caller changed or authorization timed out.')
        authorize()


def main():
    if os.getuid() != 0 or os.geteuid() != 0:
        raise PermissionError('Physical connection mediation requires its root service')
    DBusGMainLoop(set_as_default=True)
    bus = dbus.bus.BusConnection('unix:path=/run/dbus/system_bus_socket')
    unit_name = 'karyo-physical-network.service'
    expected = '/system.slice/' + unit_name
    if Path('/proc/self/cgroup').read_text().splitlines() != ['0::' + expected]:
        raise PermissionError('Physical mediation requires its fixed system service')
    manager = dbus.Interface(bus.get_object('org.freedesktop.systemd1', '/org/freedesktop/systemd1'),
                             'org.freedesktop.systemd1.Manager')
    path = manager.GetUnit(unit_name, timeout=2)
    properties = dbus.Interface(bus.get_object('org.freedesktop.systemd1', path), 'org.freedesktop.DBus.Properties')
    settings = properties.GetAll('org.freedesktop.systemd1.Service', timeout=2)
    if (str(settings['ControlGroup']) != expected or int(settings['MainPID']) != os.getpid()
            or settings['PrivateNetwork'] or str(settings['NetworkNamespacePath'])
            or not settings['NoNewPrivileges'] or int(settings['CapabilityBoundingSet']) != 0):
        raise PermissionError('The fixed physical service launch policy changed')
    service = PhysicalNetwork(bus)
    GLib.MainLoop().run()
