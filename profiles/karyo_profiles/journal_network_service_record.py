# SPDX-License-Identifier: GPL-3.0-or-later
"""Versioned ownership of the private standard network-service worker."""
from .journal_dns_record import identity, validate_unit


def validate_network_service(value):
    required = {'kind', 'pending', 'state', 'config', 'unit'}
    if (not isinstance(value, dict) or not required <= set(value) or set(value)-required-{'clients'}
            or 'clients' in value and value['clients'] is not True
            or value['kind'] != 'network-manager'):
        raise ValueError('Unsupported private network service receipt')
    pending = value['pending']
    if pending is not None and (not isinstance(pending, str) or pending not in
                               {'state-prepare', 'config-create', 'service-start', 'uplink-prepare', 'service-stop', 'config-remove'}):
        raise ValueError('Invalid private network service transition')
    state, config, unit = value['state'], value['config'], value['unit']
    if state is None:
        if pending != 'state-prepare' or config is not None or unit is not None:
            raise ValueError('Missing private network service state')
        return
    if not isinstance(state, dict) or set(state) != {'directory', 'stamp', 'home'}:
        raise ValueError('Invalid private network service state receipt')
    for item in state.values():identity(item)
    if pending == 'state-prepare':raise ValueError('Ambiguous private state preparation')
    if config is None:
        if pending != 'config-create' or unit is not None:raise ValueError('Missing private service configuration')
        return
    if (not isinstance(config, dict) or set(config) != {'directory', 'file', 'backend'}
            or not isinstance(config['backend'], str) or config['backend'] not in {'keyfile', 'netplan'}):
        raise ValueError('Invalid private network service configuration')
    identity(config['directory']);identity(config['file'])
    if pending == 'config-create':raise ValueError('Ambiguous private configuration creation')
    if unit is None:
        if pending not in {'service-start', 'config-remove'}:raise ValueError('Missing private network worker')
    else:
        validate_unit(unit, 'bus')
        if pending in {'service-start', 'config-remove'}:raise ValueError('Ambiguous private worker transition')
