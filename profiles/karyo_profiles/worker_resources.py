# SPDX-License-Identifier: GPL-3.0-or-later
"""Package-declared cleanup selectors inside one already captured namespace."""
from dataclasses import dataclass
import json
import re


@dataclass(frozen=True)
class Resources:
    interfaces: tuple
    route_tables: tuple
    rule_tables: tuple
    route_protocols: tuple
    nft_tables: tuple

    @classmethod
    def parse(cls, value):
        fields = {'interfaces', 'route_tables', 'rule_tables', 'route_protocols', 'nft_tables'}
        if not isinstance(value, dict) or set(value) != fields:
            raise ValueError('A worker needs explicit cleanup resource selectors')
        interfaces = value['interfaces']
        if (not isinstance(interfaces, list) or len(interfaces) > 1
                or any(not isinstance(name, str) or re.fullmatch(r'[A-Za-z0-9_.-]{1,15}', name) is None
                       or name in {'lo', 'eth0'} for name in interfaces)):
            raise ValueError('A worker may declare one non-base virtual tunnel name')
        numbers = {}
        for kind in ('route_tables', 'rule_tables', 'route_protocols'):
            items = value[kind]
            if not isinstance(items, list) or len(items) > 16:
                raise ValueError('Invalid bounded numeric resource selector')
            rows = []
            for item in items:
                required = {'family', 'table'} | ({'protocol'} if kind == 'route_protocols' else set())
                if (not isinstance(item, dict) or set(item) != required
                        or item['family'] not in {'ipv4', 'ipv6'}):
                    raise ValueError('Invalid worker route family selector')
                table = item['table']
                if (type(table) is not int or not 1 <= table < 2**32
                        or table in {253, 255} or table == 254 and kind != 'route_protocols'):
                    raise ValueError('A worker cannot claim an entire base routing table')
                row = (item['family'], table)
                if kind == 'route_protocols':
                    protocol = item['protocol']
                    if type(protocol) is not int or not 16 <= protocol <= 255:
                        raise ValueError('Invalid worker-owned route protocol')
                    row += (protocol,)
                rows.append(row)
            if len(set(rows)) != len(rows):
                raise ValueError('Duplicate worker numeric selector')
            numbers[kind] = tuple(sorted(rows))
        nft = value['nft_tables']
        if (not isinstance(nft, list) or len(nft) > 8
                or any(not isinstance(item, list) or len(item) != 2
                       or item[0] not in {'ip', 'ip6', 'inet'}
                       or not isinstance(item[1], str)
                       or re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,63}', item[1]) is None for item in nft)):
            raise ValueError('Invalid private worker nft table selector')
        tables = tuple(sorted(tuple(item) for item in nft))
        if len(set(tables)) != len(tables):
            raise ValueError('Duplicate worker nft table selector')
        return cls(tuple(interfaces), nft_tables=tables, **numbers)

    def record(self):
        value = {'interfaces': list(self.interfaces), 'nft_tables': [list(item) for item in self.nft_tables]}
        for name in ('route_tables', 'rule_tables'):
            value[name] = [{'family': family, 'table': table} for family, table in getattr(self, name)]
        value['route_protocols'] = [{'family': family, 'table': table, 'protocol': protocol}
                                   for family, table, protocol in self.route_protocols]
        return value

    def conflicts(self, other):
        mine = set(self.route_tables + self.rule_tables)
        theirs = set(other.route_tables + other.rule_tables)
        return (bool(set(self.interfaces) & set(other.interfaces)) or bool(mine & theirs)
                or bool(set(self.route_protocols) & set(other.route_protocols))
                or bool({row[:2] for row in self.route_protocols} & theirs)
                or bool({row[:2] for row in other.route_protocols} & mine)
                or bool(set(self.nft_tables) & set(other.nft_tables)))

    def present(self, network):
        network.verify()
        def decode(text):
            if len(text.encode('utf-8')) > 16*1024*1024:
                raise RuntimeError('The private resource inventory exceeds its bound')
            return json.loads(text)
        links = decode(network._ip('-j', 'link', 'show', inside=True))
        routes, rules = {}, {}
        for family, flag in (('ipv4', '-4'), ('ipv6', '-6')):
            routes[family] = decode(network._ip('-j', '-N', flag, 'route', 'show', 'table', 'all', inside=True))
            rules[family] = decode(network._ip('-j', '-N', flag, 'rule', 'show', inside=True))
        nft = decode(network.command.run('/usr/bin/nsenter', '--net='+network.path, '--',
                                        '/usr/sbin/nft', '-j', 'list', 'tables'))
        if (not isinstance(links, list) or any(not isinstance(rows, list) for rows in (*routes.values(), *rules.values()))
                or not isinstance(nft, dict) or not isinstance(nft.get('nftables'), list)):
            raise RuntimeError('Invalid native private resource inventory')
        def number(value):
            if type(value) is int:
                return value
            if isinstance(value, str) and value.isascii() and value.isdecimal():
                return int(value)
            raise RuntimeError('The native numeric resource inventory changed its format')
        route_tables = {(family, number(row.get('table', 254)))
                        for family, rows in routes.items() for row in rows}
        rule_tables = {(family, number(row['table']))
                       for family, rows in rules.items() for row in rows if 'table' in row}
        protocols = {(family, number(row.get('table', 254)), number(row['protocol']))
                     for family, rows in routes.items() for row in rows if 'protocol' in row}
        tables = {(row['table']['family'], row['table']['name'])
                  for row in nft['nftables'] if isinstance(row, dict) and 'table' in row}
        network.verify()
        return {'interfaces': sorted(set(self.interfaces) & {row['ifname'] for row in links}),
                'route_tables': [list(row) for row in sorted(set(self.route_tables) & route_tables)],
                'rule_tables': [list(row) for row in sorted(set(self.rule_tables) & rule_tables)],
                'route_protocols': [list(row) for row in sorted(set(self.route_protocols) & protocols)],
                'nft_tables': [list(item) for item in sorted(set(self.nft_tables) & tables)]}

    def absent(self, network):
        observed = self.present(network)
        if any(observed.values()):
            raise RuntimeError('Declared worker resources are occupied or not retired')
        return observed
