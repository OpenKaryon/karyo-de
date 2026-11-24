# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned QA-only physical DNS endpoint addresses, outside the desktop pool."""
import json

from native_main_network import require, run


class UnderlayAddress:
    def __init__(self, address, token):
        self.address, self.label = address, 'lo:kq' + token[:8]
        self.interface = self.row = None

    def inventory(self):
        rows = json.loads(run('/usr/sbin/ip', '-j', '-4', 'address', 'show', 'dev', 'lo'))
        require(len(rows) == 1 and rows[0]['ifname'] == 'lo', 'The QA loopback identity changed')
        return rows[0]['ifindex'], [row for row in rows[0]['addr_info'] if row.get('local') == self.address]

    def create(self):
        interface, rows = self.inventory()
        require(not rows, 'An existing physical fixture address is retained')
        run('/usr/sbin/ip', 'address', 'add', self.address + '/32', 'dev', 'lo', 'label', self.label)
        current, rows = self.inventory()
        require(current == interface and len(rows) == 1 and rows[0].get('label') == self.label
                and rows[0].get('prefixlen') == 32, 'The new physical fixture address was not captured')
        self.interface, self.row = current, rows[0]

    def close(self):
        if self.row is None:return
        interface, rows = self.inventory()
        require(interface == self.interface and rows == [self.row], 'The captured physical fixture address changed')
        run('/usr/sbin/ip', 'address', 'delete', self.address + '/32', 'dev', 'lo')
        self.row = None
