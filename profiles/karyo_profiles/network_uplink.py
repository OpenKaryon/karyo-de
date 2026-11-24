# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the context's ordinary, non-persistent NM base connection."""
import json
import re
import uuid

from .services import ServiceError


def uplink_uuid(scope):
    # A connection identifier, never authority to select a namespace or bus.
    return str(uuid.UUID(hex=scope.network.spec.token))


def prepare_uplink(scope):
    spec, seed = scope.network.spec, scope.files.plan.seed
    identity = uplink_uuid(scope)
    existing = scope.nmcli('--terse', '--fields', 'UUID', 'connection', 'show').splitlines()
    if identity in existing:
        raise ServiceError('An unrelated connection occupies the generated uplink identity')
    scope.nmcli('connection', 'add', 'save', 'no', 'type', 'ethernet', 'ifname', 'eth0',
                'con-name', 'Karyo uplink', 'connection.uuid', identity,
                'connection.autoconnect', 'no', 'ipv4.method', 'manual',
                'ipv4.addresses', spec.guest + '/30', 'ipv4.gateway', spec.host,
                'ipv4.dns', ','.join(seed.servers), 'ipv4.dns-search', ','.join(seed.search),
                'ipv6.method', 'disabled')
    scope.nmcli('connection', 'up', 'uuid', identity)
    active = scope.nmcli('--terse', '--fields', 'UUID,DEVICE', 'connection', 'show', '--active').splitlines()
    if identity + ':eth0' not in active:
        raise ServiceError('The generated base connection did not activate on its captured uplink')
    if not uplink_flags(scope) & 1:
        raise ServiceError('The generated uplink was unexpectedly saved as a persistent connection')


def uplink_flags(scope):
    scope.verify()
    command = scope.network.command
    prefix = ('/usr/bin/busctl', '--address=' + scope.files.plan.bus_address, '--json=short', '--timeout=2s')
    value = json.loads(command.run(*prefix, 'call', 'org.freedesktop.NetworkManager',
        '/org/freedesktop/NetworkManager/Settings', 'org.freedesktop.NetworkManager.Settings',
        'GetConnectionByUuid', 's', uplink_uuid(scope)))
    if (value.get('type') != 'o' or not isinstance(value.get('data'), list) or len(value['data']) != 1
            or re.fullmatch(r'/org/freedesktop/NetworkManager/Settings/[0-9]+', value['data'][0]) is None):
        raise ServiceError('Invalid generated uplink connection object')
    flags = json.loads(command.run(*prefix, 'get-property', 'org.freedesktop.NetworkManager', value['data'][0],
                                  'org.freedesktop.NetworkManager.Settings.Connection', 'Flags'))
    if flags.get('type') != 'u' or type(flags.get('data')) is not int:
        raise ServiceError('Invalid generated uplink persistence flags')
    scope.verify()
    return flags['data']
