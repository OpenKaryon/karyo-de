#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Serve text, PNG and files with X11 timestamps and incremental transfers.

open-vm-tools compares CLIPBOARD and PRIMARY timestamps before choosing what
to send to Windows. xclip offers no TIMESTAMP and can make a newly copied
screenshot lose to an older PRIMARY selection. This owner never changes
PRIMARY. It is a child of the Wayland bridge and exits when ownership ends.
"""
import os
import select
import sys
import time

from Xlib import X, Xatom, display
from Xlib.protocol import event

PNG = 'image/png'
TEXT = 'text/plain;charset=utf-8'
URI = 'text/uri-list'
GNOME = 'x-special/gnome-copied-files'
LIMITS = {PNG: 32 << 20, TEXT: 1 << 20, URI: 1 << 20}


class ClipboardOwner:
    def __init__(self, mime, data, selection='CLIPBOARD'):
        self.mime, self.data = mime, data
        self.display = display.Display()
        screen = self.display.screen()
        self.window = screen.root.create_window(
            0, 0, 1, 1, 0, screen.root_depth, X.InputOutput, X.CopyFromParent,
            event_mask=X.PropertyChangeMask)
        self.atoms = {name: self.display.intern_atom(name) for name in (
            'CLIPBOARD', 'TARGETS', 'TIMESTAMP', 'MULTIPLE', 'ATOM_PAIR', 'INCR',
            'image/png', 'UTF8_STRING', TEXT, 'text/plain', 'STRING', URI, GNOME, '_KARYO_CLIPBOARD_CLOCK')}
        self.atoms['CLIPBOARD'] = self.display.intern_atom(selection)
        self.chunk_size = min(65536, self.display.display.info.max_request_length * 4 - 128)
        self.transfers = {}
        self.owned = True
        self.timestamp = self.server_time()
        self.window.set_selection_owner(self.atoms['CLIPBOARD'], self.timestamp)
        self.display.sync()
        if self.display.get_selection_owner(self.atoms['CLIPBOARD']).id != self.window.id:
            raise RuntimeError('could not acquire the clipboard')

    def server_time(self):
        clock = self.atoms['_KARYO_CLIPBOARD_CLOCK']
        self.window.change_property(clock, Xatom.INTEGER, 32, [0])
        self.display.flush()
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if not self.display.pending_events():
                select.select([self.display], [], [], max(0, deadline - time.monotonic()))
                continue
            notification = self.display.next_event()
            if notification.type == X.PropertyNotify and notification.atom == clock:
                return notification.time
        raise RuntimeError('could not obtain the X server timestamp')

    def targets(self):
        names = ['TARGETS', 'TIMESTAMP', 'MULTIPLE']
        if self.mime == URI:
            names += [URI, GNOME]
        elif self.mime == PNG:
            names += [PNG]
        else:
            names += ['UTF8_STRING', TEXT, 'text/plain', 'STRING']
        return [self.atoms[name] for name in names]

    def convert(self, requestor, target, prop):
        if target == self.atoms['TARGETS']:
            requestor.change_property(prop, Xatom.ATOM, 32, self.targets())
            return True
        if target == self.atoms['TIMESTAMP']:
            requestor.change_property(prop, Xatom.INTEGER, 32, [self.timestamp])
            return True
        data = self.data
        if self.mime == URI:
            if target == self.atoms[GNOME]:
                data = b'copy\n' + b'\n'.join(data.splitlines())
            elif target != self.atoms[URI]:
                return False
        elif self.mime == PNG:
            if target != self.atoms[PNG]:
                return False
        elif target == self.atoms['STRING']:
            data = data.decode('utf-8', errors='replace').encode('latin-1', errors='replace')
        elif target not in (self.atoms['UTF8_STRING'], self.atoms[TEXT], self.atoms['text/plain']):
            return False
        if len(data) <= self.chunk_size:
            requestor.change_property(prop, target, 8, data)
        else:
            requestor.change_attributes(event_mask=X.PropertyChangeMask | X.StructureNotifyMask)
            if len(self.transfers) >= 32 and (requestor.id, prop) not in self.transfers:
                return False
            self.transfers[(requestor.id, prop)] = [requestor, target, data, 0, time.monotonic()]
            requestor.change_property(prop, self.atoms['INCR'], 32, [len(data)])
        return True

    def requested(self, request):
        prop = request.property or request.target
        accepted = False
        try:
            if request.selection == self.atoms['CLIPBOARD']:
                if request.target == self.atoms['MULTIPLE'] and request.property:
                    pairs = request.requestor.get_property(prop, self.atoms['ATOM_PAIR'], 0, 2048)
                    if pairs and not pairs.bytes_after and pairs.format == 32 and len(pairs.value) % 2 == 0 and len(pairs.value) <= 2048:
                        values = list(pairs.value)
                        for i in range(0, len(values), 2):
                            if (not values[i + 1] or values[i] == self.atoms['MULTIPLE']
                                    or not self.convert(request.requestor, values[i], values[i + 1])):
                                values[i + 1] = X.NONE
                        request.requestor.change_property(prop, self.atoms['ATOM_PAIR'], 32, values)
                        accepted = True
                else:
                    accepted = self.convert(request.requestor, request.target, prop)
            request.requestor.send_event(event.SelectionNotify(
                time=request.time, requestor=request.requestor, selection=request.selection,
                target=request.target, property=prop if accepted else X.NONE), event_mask=0)
        except Exception:
            # A requestor can vanish at any point in a selection transfer.
            pass

    def handle(self, notification):
        if notification.type == X.SelectionRequest:
            self.requested(notification)
        elif notification.type == X.SelectionClear:
            self.owned = False
        elif notification.type == X.DestroyNotify:
            self.transfers = {key: value for key, value in self.transfers.items() if key[0] != notification.window.id}
        elif notification.type == X.PropertyNotify and notification.state == X.PropertyDelete:
            key = (notification.window.id, notification.atom)
            transfer = self.transfers.get(key)
            if transfer:
                requestor, target, data, offset, _changed = transfer
                chunk = data[offset:offset + self.chunk_size]
                requestor.change_property(notification.atom, target, 8, chunk)
                if chunk:
                    transfer[3] += len(chunk)
                    transfer[4] = time.monotonic()
                else:
                    del self.transfers[key]

    def run(self):
        while self.owned or self.transfers:
            while self.display.pending_events():
                self.handle(self.display.next_event())
            self.display.flush()
            now = time.monotonic()
            self.transfers = {key: value for key, value in self.transfers.items() if now - value[4] < 15}
            if self.owned or self.transfers:
                select.select([self.display], [], [], .5)

    def close(self):
        self.display.close()


def main():
    if sys.argv[1:] == ['--primary-timestamp']:
        print(primary_timestamp())
        return 0
    mime = sys.argv[1] if len(sys.argv) == 2 else ''
    if mime not in LIMITS:
        return 2
    data = sys.stdin.buffer.read(LIMITS[mime] + 1)
    if len(data) > LIMITS[mime] or (mime == PNG and not data.startswith(b'\x89PNG\r\n\x1a\n')):
        return 2
    owner = ClipboardOwner(mime, data)
    try:
        print(f'READY {owner.window.id} {owner.timestamp}', flush=True)
        sys.stdout.close()
        owner.run()
    finally:
        owner.close()
    return 0


def primary_timestamp():
    connection = display.Display()
    try:
        atom = connection.intern_atom
        if not connection.get_selection_owner(atom('PRIMARY')):
            return 0
        window = connection.screen().root.create_window(0, 0, 1, 1, 0, X.CopyFromParent, X.InputOnly)
        prop = atom('_KARYO_PRIMARY_TIME')
        window.convert_selection(atom('PRIMARY'), atom('TIMESTAMP'), prop, X.CurrentTime)
        connection.flush()
        deadline = time.monotonic() + .5
        while time.monotonic() < deadline:
            if not connection.pending_events():
                select.select([connection], [], [], max(0, deadline - time.monotonic()))
                continue
            reply = connection.next_event()
            if reply.type == X.SelectionNotify:
                if not reply.property:
                    return 0
                value = window.get_property(prop, X.AnyPropertyType, 0, 4)
                if value and value.format == 32 and value.property_type in (Xatom.INTEGER, atom('TIMESTAMP')) and len(value.value):
                    return int(value.value[0])
                return 0
        return 0
    finally:
        connection.close()


if __name__ == '__main__':
    sys.exit(main())
