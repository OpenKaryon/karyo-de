# SPDX-License-Identifier: GPL-3.0-or-later
"""Physical NetworkManager operations; no host VPN or arbitrary command API."""
from dataclasses import dataclass
from pathlib import Path
import re
import uuid
from typing import Optional

NM = 'org.freedesktop.NetworkManager'
ROOT = '/org/freedesktop/NetworkManager'
DEVICE = NM + '.Device'
WIRELESS = DEVICE + '.Wireless'
ACTIVE = NM + '.Connection.Active'
SETTINGS = NM + '.Settings.Connection'
KINDS = {1: '802-3-ethernet', 2: '802-11-wireless'}


def physical_device(interface):
    if not isinstance(interface, str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,15}', interface):
        raise ValueError('Invalid physical interface')
    path = Path('/sys/class/net') / interface
    # veth, bridge, loopback and VPN interfaces have no hardware device parent.
    parent = (path / 'device').stat()
    return int((path / 'ifindex').read_text()), parent.st_dev, parent.st_ino

def external_default(interface):
    for line in Path('/proc/net/route').read_text().splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 8 and fields[0] == interface and fields[1] == fields[7] == '00000000':
            if int(fields[3], 16) & 3 == 3:return True
    return False

def security(flags, wpa, rsn):
    if (wpa | rsn) & 0x200:return '802.1X'
    if rsn & 0x100:return 'WPA2'
    if rsn & 0x400:return 'WPA3'
    if wpa & 0x100:return 'WPA1'
    if rsn & 0x1800:return 'OWE'
    return 'WEP' if flags & 1 else '--'


def password_valid(kind, value):
    if not isinstance(value, str):return False
    if kind in ('--', 'OWE'):return value == ''
    if kind == 'WPA3':return 0 < len(value.encode('utf-8')) <= 63 and '\0' not in value
    return bool(re.fullmatch(r'[ -~]{8,63}|[0-9A-Fa-f]{64}', value)) if kind in ('WPA1', 'WPA2') else False


def packet(value):
    if not isinstance(value, dict) or type(value.get('version')) is not int or value['version'] != 1:
        raise ValueError('Unsupported physical network request')
    op = value.get('operation')
    required = {'radio': {'enabled'}, 'activate': {'uuid'}, 'disconnect': {'uuid'},
                'wifi': {'interface', 'access_point', 'password'}}
    if not isinstance(op, str) or op not in required or set(value) != {'version', 'operation', 'owner'} | required[op]:
        raise ValueError('Unsupported physical network operation')
    if not isinstance(value['owner'], str) or not re.fullmatch(r':[0-9]+\.[0-9]+', value['owner']):
        raise ValueError('Invalid network manager snapshot')
    if op == 'radio':
        if type(value['enabled']) is not bool:raise ValueError('Invalid radio state')
    elif op in ('activate', 'disconnect'):
        if not isinstance(value['uuid'], str) or str(uuid.UUID(value['uuid'])) != value['uuid']:
            raise ValueError('Invalid physical connection UUID')
    else:
        if not isinstance(value['password'], str) or len(value['password'].encode('utf-8')) > 64:
            raise ValueError('Invalid Wi-Fi password input')
        if not isinstance(value['access_point'], str) or not re.fullmatch(ROOT + r'/AccessPoint/[0-9]+', value['access_point']):
            raise ValueError('Invalid Wi-Fi access point')
        if not isinstance(value['interface'], str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,15}', value['interface']):
            raise ValueError('Invalid Wi-Fi interface')
    return value


@dataclass
class Plan:
    request: dict
    device: Optional[dict]
    connection: Optional[tuple] = None
    access_point: Optional[dict] = None

    @property
    def actions(self):
        if self.request['operation'] == 'radio':return ('enable-disable-wifi',)
        return ('network-control', 'settings.modify.system') if self.access_point is not None else ('network-control',)


class PhysicalBackend:
    def __init__(self, api, *, kernel=physical_device, external=external_default):
        self.api, self.kernel, self.external = api, kernel, external

    def devices(self):
        result = []
        for path in self.api.call(ROOT, NM, 'GetDevices')[:64]:
            try:
                p = self.api.properties(path, DEVICE)
                kind, interface = int(p['DeviceType']), str(p['Interface'])
                if kind not in KINDS:continue
                identity = self.kernel(interface)
            except (FileNotFoundError, KeyError):continue
            result.append({'path': str(path), 'interface': interface, 'kind': kind,
                           'identity': identity, 'state': int(p['State']), 'managed': bool(p.get('Managed', False))})
        return result

    def profiles(self):
        result = []
        for path in self.api.call(ROOT + '/Settings', NM + '.Settings', 'ListConnections')[:128]:
            settings = self.api.call(path, SETTINGS, 'GetSettings')
            p = settings.get('connection', {})
            if str(p.get('type')) not in KINDS.values() or p.get('master') or p.get('controller'):continue
            identity = str(p.get('uuid', ''))
            if str(uuid.UUID(identity)) != identity:raise ValueError('Invalid physical connection identity')
            result.append((str(path), settings))
        return result

    def points(self, device):
        if device['kind'] != 2:return []
        result = []
        for path in self.api.call(device['path'], WIRELESS, 'GetAllAccessPoints')[:128]:
            p = self.api.properties(path, NM + '.AccessPoint')
            raw = bytes(p['Ssid'])
            if not 0 < len(raw) <= 32:continue
            flags, wpa, rsn = int(p['Flags']), int(p['WpaFlags']), int(p['RsnFlags'])
            result.append({'path': str(path), 'ssid': raw, 'security': security(flags, wpa, rsn),
                           'flags': (flags, wpa, rsn), 'strength': int(p['Strength'])})
        return result

    def active(self, devices):
        result = []
        for path in self.api.properties(ROOT, NM).get('ActiveConnections', ())[:64]:
            p = self.api.properties(path, ACTIVE)
            if str(p.get('Type')) not in KINDS.values():continue
            matches = [d for d in devices if d['path'] in p.get('Devices', ())]
            if len(matches) != 1:continue
            result.append((str(path), str(p['Uuid']), str(p['Id']), matches[0]))
        return result

    def inventory(self):
        self.api.renew()
        devices = self.devices();active = self.active(devices);profiles = self.profiles()
        manager = self.api.properties(ROOT, NM)
        connections, points = [], []
        for path, settings in profiles:
            p = settings['connection'];kind = str(p['type']);interface = str(p.get('interface-name', ''))
            matches = [d for d in devices if d['managed'] and KINDS[d['kind']] == kind and (not interface or d['interface'] == interface)]
            if not matches:continue
            running = next((a for a in active if a[1] == str(p['uuid'])), None)
            connections.append({'uuid': str(p['uuid']), 'name': str(p['id']), 'type': kind,
                                'active': running is not None, 'interface': running[3]['interface'] if running else ''})
        for d in devices:
            if not d['managed']:
                connections.append({'uuid': '', 'name': d['interface'], 'type': KINDS[d['kind']],
                    'active': self.external(d['interface']), 'interface': d['interface'], 'external': True})
                continue
            for p in self.points(d):
                points.append({'interface': d['interface'], 'path': p['path'], 'ssid': p['ssid'].decode('utf-8', 'replace'),
                               'security': p['security'], 'strength': p['strength']})
        current = next((a for a in active if a[3]['state'] == 100), None)
        outside = next((d for d in devices if not d['managed'] and self.external(d['interface'])), None)
        self.api.verify()
        return {'version': 1, 'owner': self.api.owner, 'connected': current is not None or outside is not None,
                'name': current[2] if current else outside['interface'] if outside else '',
                'type': KINDS[current[3]['kind']] if current else KINDS[outside['kind']] if outside else '',
                'wireless': any(d['kind'] == 2 for d in devices),
                'wireless_enabled': bool(manager.get('WirelessEnabled', False)),
                'connections': connections, 'access_points': points}

    def prepare(self, request):
        self.api.renew();request = packet(request);devices = [d for d in self.devices() if d['managed']];op = request['operation']
        if request['owner'] != self.api.owner:raise ValueError('The network list changed; refresh it')
        if op == 'radio':
            if not any(d['kind'] == 2 for d in devices):raise ValueError('No physical Wi-Fi device')
            return Plan(request, None)
        if op == 'wifi':
            device = next(d for d in devices if d['interface'] == request['interface'] and d['kind'] == 2)
            point = next(p for p in self.points(device) if p['path'] == request['access_point'])
            if not password_valid(point['security'], request['password']):raise ValueError('Unsupported Wi-Fi credentials')
            return Plan(request, device, access_point=point)
        connection = next(c for c in self.profiles() if str(c[1]['connection']['uuid']) == request['uuid'])
        p = connection[1]['connection'];kind = str(p['type']);interface = str(p.get('interface-name', ''))
        if op == 'disconnect':
            device = next(a[3] for a in self.active(devices) if a[1] == request['uuid'])
        else:
            device = next(d for d in devices if KINDS[d['kind']] == kind and (not interface or d['interface'] == interface))
        return Plan(request, device, connection=connection)

    def execute(self, plan):
        self.api.renew();self.api.verify()
        fresh = self.prepare(plan.request)
        if plan.device is not None and any(fresh.device[k] != plan.device[k] for k in ('path', 'interface', 'kind', 'identity')):
            raise ValueError('The physical device changed during authorization')
        if fresh.connection != plan.connection:raise ValueError('The saved physical connection changed')
        if plan.access_point is not None and any(fresh.access_point[k] != plan.access_point[k] for k in ('path', 'ssid', 'security', 'flags')):
            raise ValueError('The Wi-Fi access point changed during authorization')
        op = plan.request['operation']
        if op == 'radio':
            self.api.set_radio(plan.request['enabled']);return None
        if op == 'disconnect':
            self.api.call(plan.device['path'], DEVICE, 'Disconnect');return None
        if op == 'activate':
            return str(self.api.activate(plan.connection[0], plan.device['path']))
        return str(self.api.connect_wifi(plan.device['path'], plan.access_point, plan.request['password']))

    def activation(self, plan, path):
        self.api.renew();self.api.verify()
        if self.kernel(plan.device['interface']) != plan.device['identity']:
            raise ValueError('The physical device changed during activation')
        active = self.api.properties(path, ACTIVE)
        if int(active['State']) == 2:return True, ''
        device = self.api.properties(plan.device['path'], DEVICE)
        if int(device['State']) == 120 or int(active['State']) == 4:
            message = ('Wi-Fi authentication failed. Check the password and signal.' if plan.access_point is not None
                       else 'The network disconnected before activation finished.')
            return True, message
        return False, ''
