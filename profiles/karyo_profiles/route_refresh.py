# SPDX-License-Identifier: GPL-3.0-or-later
"""Journal an owned carrier transition while private desktop processes stay live."""
from copy import deepcopy
import ipaddress
import json

from .dns_refresh import encoded, refresh_dns
from .forwarding import PRIVATE_POOL, ProfileForwarding, _canonical
from .journal import JournalRetained
from .journal_dns_record import seed
from .journal_forwarding import firewall_absent, apply_proof, receipt
from .underlay import discover_underlay


def validate_refresh(value):
    if (not isinstance(value, dict) or set(value) != {'target', 'source_dns', 'step'}
            or type(value['step']) is not int or not 0 <= value['step'] <= 7):
        raise ValueError('Invalid carrier refresh intent')
    seed(value['source_dns'])
    target = seed(value['target'])
    if ipaddress.IPv4Network(target.physical.subnet).overlaps(PRIVATE_POOL):
        raise ValueError('The replacement carrier overlaps the private desktop pool')
    return target


def route_key(row):
    value = dict(row)
    flags = value.pop('flags', [])
    if not isinstance(flags, list) or any(flag not in ('linkdown', 'dead') for flag in flags):
        raise JournalRetained('Unexpected native route flags during carrier refresh')
    return json.dumps(value, sort_keys=True, separators=(',', ':'))


def policy(rows):
    result = deepcopy(rows)
    for row in result:
        if 'rule' in row:
            handle = row['rule'].pop('handle', None)
            if type(handle) is not int or handle <= 0:
                raise JournalRetained('A carrier firewall rule lost its native handle')
    return result


def redirected(rows, original, target):
    result = policy(rows)
    def visit(value):
        if isinstance(value, dict):
            match = value.get('match')
            if (isinstance(match, dict) and match.get('left') in
                    ({'meta': {'key': 'iifname'}}, {'meta': {'key': 'oifname'}})
                    and match.get('right') == original):
                match['right'] = target
            for item in value.values():
                visit(item)
        elif isinstance(value, list):
            for item in value:
                visit(item)
    visit(result)
    return result


def paused_policy(rows, gateway):
    original = policy(rows)
    result = []
    inserted = False
    for row in original:
        if 'rule' not in row or row['rule'].get('chain') != 'forward':
            result.append(row)
            continue
        if inserted:
            continue
        inserted = True
        for key in ('iifname', 'oifname'):
            result.append({'rule': {'family': 'inet', 'table': gateway.name,
                'chain': 'forward', 'comment': gateway.network.spec.alias + ':refresh',
                'expr': [{'match': {'op': '==', 'left': {'meta': {'key': key}},
                                  'right': gateway.network.spec.interface}}, {'drop': None}]}})
    if not inserted:
        raise JournalRetained('The captured forwarding policy has no forward rules')
    return result


class RouteRefresh:
    def __init__(self, forwarding, *, dns=None, current=discover_underlay):
        self.forwarding, self.dns, self.current = forwarding, dns, current
        self.gateway = forwarding.gateway
        self.journal, self.token = forwarding.journal, forwarding.token
        value = self.journal.record(self.token)['forwarding']
        self.original = deepcopy(value)
        self.intent = deepcopy(value['refresh'])
        self.target = validate_refresh(self.intent)
        self.desired = ProfileForwarding(self.gateway.network, self.target.physical)
        self.old_routes = value['proof']['routes']
        self.plans = list(zip(self.gateway.route_plan(), self.old_routes))
        self.new_routes = []
        for index, (arguments, fields) in enumerate(self.desired.route_plan()):
            row = dict(self.old_routes[index])
            row.update(fields)
            self.new_routes.append(row)
            self.plans.append(((arguments, fields), row))
        self.base = {route_key(self.old_routes[index]) for index in (0, 2)}
        self.allowed = {route_key(row): arguments for (arguments, fields), row in self.plans}

    def step(self, value):
        before = self.journal.record(self.token)
        self.intent = dict(self.intent, step=value)
        self.journal.change(before, dict(before, forwarding=dict(before['forwarding'], refresh=self.intent)))

    def verify(self):
        self.gateway._guard()
        value = self.journal.record(self.token)['forwarding']
        if value['refresh'] != self.intent or value['proof'] != self.original['proof']:
            raise JournalRetained('The carrier transition receipt changed')
        routes = self.gateway._routes()
        keys = [route_key(row) for row in routes]
        cleanup = self.intent['step'] == 7
        if (len(keys) != len(set(keys)) or set(keys) - set(self.allowed)
                or not cleanup and not self.base <= set(keys)):
            raise JournalRetained('The carrier routing table differs from its captured plans')
        expected_rules = value['proof']['rules']
        rules = [row for row in self.gateway._rules() if row.get('priority') in self.gateway.priorities
                 or str(row.get('table')) == str(self.gateway.table)]
        if ((not cleanup and _canonical(rules) != _canonical(expected_rules))
                or cleanup and any(row not in expected_rules for row in rules)):
            raise JournalRetained('The carrier rule ownership changed')
        try:
            rows, handle = self.gateway._firewall()
        except RuntimeError:
            if not cleanup or not firewall_absent(self.gateway):
                raise
            return routes, rules, None
        if handle != value['proof']['firewall']['handle']:
            raise JournalRetained('The carrier firewall table was replaced')
        captured = value['proof']['firewall']['snapshot']
        actual = policy(rows)
        states = []
        if actual == policy(captured):
            states.append('original')
        if actual == paused_policy(captured, self.gateway):
            states.append('paused')
        if actual == redirected(captured, self.gateway.physical.interface, self.target.physical.interface):
            states.append('final')
        accepted = ({'original', 'paused'} if self.intent['step'] == 0 else
                    {'paused'} if self.intent['step'] < 5 else {'paused', 'final'})
        if cleanup:
            accepted.add('original')
        if not accepted.intersection(states):
            raise JournalRetained('The carrier firewall policy differs from its transition')
        return routes, rules, states

    def pause(self):
        unused_routes, unused_rules, states = self.verify()
        if 'paused' not in states:
            self.gateway._nft('-f', '-', input_data=self.gateway.replacement_script(paused=True))
        # Step zero also accepts the applied next action before its journal ACK.
        if 'paused' not in self.verify()[2]:
            raise JournalRetained('The carrier pause was not acknowledged')
        if self.intent['step'] == 0:
            self.step(1)

    def replace_route(self, index):
        routes = self.verify()[0]
        expected = route_key(self.new_routes[index])
        if not any(route_key(row) == expected for row in routes):
            arguments = self.desired.route_plan()[index][0]
            self.gateway._ip('route', 'replace', *arguments, 'table', str(self.gateway.table), 'proto', 'static')
        if not any(route_key(row) == expected for row in self.verify()[0]):
            raise JournalRetained('The carrier route replacement was not acknowledged')

    def remove_physical(self, *, all_routes=False):
        desired = {route_key(row) for row in self.new_routes}
        for row in self.verify()[0]:
            key = route_key(row)
            if key in self.base or not all_routes and key in desired:
                continue
            self.gateway._ip('route', 'delete', *self.allowed[key],
                'table', str(self.gateway.table), 'proto', 'static')
            self.verify()

    def retarget(self, target):
        if self.intent['step'] == 7:
            raise JournalRetained('A retiring carrier transition cannot reconnect')
        self.pause()
        if 'refresh' in self.journal.record(self.token)['dns']:
            refresh_dns(self.dns, forwarding=self.forwarding)
        self.remove_physical(all_routes=True)
        before = self.journal.record(self.token)
        intent = {'source_dns': before['dns']['seed'], 'target': encoded(target), 'step': 1}
        self.journal.change(before, dict(before, forwarding=dict(before['forwarding'], refresh=intent)))
        return RouteRefresh(self.forwarding, dns=self.dns, current=self.current)

    def finish(self):
        if self.dns is None or self.intent['step'] == 7:
            raise JournalRetained('A live carrier transition requires its resolver owner')
        self.verify()
        if self.intent['step'] == 0:
            self.pause()
        if self.intent['step'] == 1:
            self.replace_route(1)
            self.step(2)
        if self.intent['step'] == 2:
            self.replace_route(3)
            self.step(3)
        if self.intent['step'] == 3:
            self.remove_physical()
            self.step(4)
        if self.intent['step'] == 4:
            if 'refresh' in self.journal.record(self.token)['dns']:
                refresh_dns(self.dns, forwarding=self.forwarding)
            refresh_dns(self.dns, self.target, forwarding=self.forwarding)
            self.step(5)
        if self.intent['step'] == 5:
            if self.current(self.gateway.command).routing_identity != self.target.physical.routing_identity:
                raise JournalRetained('The physical network changed again before carrier publication')
            if 'final' not in self.verify()[2]:
                self.gateway._nft('-f', '-', input_data=self.desired.replacement_script())
            if 'final' not in self.verify()[2]:
                raise JournalRetained('The replacement carrier policy was not acknowledged')
            self.step(6)
        routes = self.verify()[0]
        if set(map(route_key, routes)) != set(map(route_key, self.new_routes)):
            raise JournalRetained('The replacement carrier route graph is incomplete')
        self.gateway.physical = self.target.physical
        proof = deepcopy(self.original['proof'])
        proof['routes'] = [next(row for row in routes if route_key(row) == route_key(expected))
                           for expected in self.new_routes]
        rows, handle = self.gateway._firewall()
        proof['firewall'] = {'handle': handle, 'snapshot': rows}
        apply_proof(self.gateway, proof)
        final = receipt(self.gateway)
        before = self.journal.record(self.token)
        self.journal.change(before, dict(before, forwarding={
            'physical': encoded(self.target)['physical'], 'pending': None, 'proof': final}))
        return True

    def close(self):
        self.gateway.network._empty()
        self.step(7)
        routes, rules, states = self.verify()
        for row in reversed(rules):
            selector = ('from', self.gateway.network.spec.guest + '/32') if 'dst' not in row else (
                'to', self.gateway.network.spec.guest + '/32')
            self.gateway._ip('rule', 'delete', 'priority', str(row['priority']),
                *selector, 'table', str(self.gateway.table))
            self.verify()
        if states is not None:
            self.gateway._nft('delete', 'table', 'inet', 'handle',
                str(self.original['proof']['firewall']['handle']))
            self.verify()
        for row in reversed(self.verify()[0]):
            self.gateway._ip('route', 'delete', *self.allowed[route_key(row)],
                'table', str(self.gateway.table), 'proto', 'static')
            self.verify()
        before = self.journal.record(self.token)
        updated = dict(before)
        updated.pop('forwarding')
        self.journal.change(before, updated)
        self.gateway.closed = True


def refresh_routes(forwarding, dns, target, *, current=discover_underlay):
    before = forwarding.journal.record(forwarding.token)
    value = before['forwarding']
    if value['pending'] is not None:
        raise JournalRetained('Carrier refresh requires an acknowledged forwarding lease')
    if 'refresh' not in value:
        intent = {'target': encoded(target), 'source_dns': before['dns']['seed'], 'step': 0}
        validate_refresh(intent)
        forwarding.journal.change(before, dict(before, forwarding=dict(value, refresh=intent)))
    transition = RouteRefresh(forwarding, dns=dns, current=current)
    if transition.target != target:
        transition = transition.retarget(target)
    return transition.finish()
