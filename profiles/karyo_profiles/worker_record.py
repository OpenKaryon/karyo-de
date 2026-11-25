# SPDX-License-Identifier: GPL-3.0-or-later
"""Durable typed worker receipts; no recorded path is launch authority."""
import hashlib
import re

from .journal_dns_record import identity, validate_unit
from .worker_declaration import Declaration

PHASES = frozenset({None, 'files-create', 'unit-start', 'ready-wait', 'retire',
    'unit-stop', 'recovery-start', 'recovery-wait', 'retained',
    'remove-runtime', 'remove-state', 'remove-prepared', 'remove-control'})
REMOVING = frozenset({'remove-runtime', 'remove-state', 'remove-prepared', 'remove-control'})


def opaque(value, length=32):
    if not isinstance(value, str) or re.fullmatch('[a-f0-9]{'+str(length)+'}', value) is None:
        raise ValueError('Invalid worker lifetime identifier')


def unique_name(value):
    if not isinstance(value, str) or re.fullmatch(r':[0-9]+\.[0-9]+', value) is None:
        raise ValueError('Invalid native worker bus identity')


def process(value):
    if (type(value['pid']) is not int or not 1 < value['pid'] < 2**31
            or type(value['start']) is not int or not 0 < value['start'] < 2**64):
        raise ValueError('Invalid captured worker process')


def origin(value, uid, namespace):
    fields = {'uid','gid','pid','start','executable','network','mount','user','cgroup','cgroup_node'}
    if not isinstance(value, dict) or set(value) != fields or value['uid'] != uid or type(value['uid']) is not int:
        raise ValueError('Invalid captured application origin')
    process(value)
    if type(value['gid']) is not int or not 0 <= value['gid'] < 2**32:
        raise ValueError('Invalid captured application group')
    for key in ('executable','network','mount','user','cgroup_node'):
        identity(value[key])
    if value['network'] != namespace:
        raise ValueError('The worker origin belongs to another desktop namespace')
    prefix = '/user.slice/user-'+str(uid)+'.slice/user@'+str(uid)+'.service/app.slice/'
    group = value['cgroup']
    if (not isinstance(group, str) or not group.startswith(prefix)
            or re.fullmatch(r'app-(?:[A-Za-z0-9_.@-]|\\x[0-9a-fA-F]{2})+\.(?:service|scope)',
                            group.removeprefix(prefix)) is None):
        raise ValueError('The worker origin is not an independent application scope')


def files(value, gid):
    if not isinstance(value, dict) or set(value) != {'state','runtime','control','prepared','sha256'}:
        raise ValueError('Invalid captured worker files')
    for key, mode, group in (('state',0o700,0),('runtime',0o750,gid)):
        row = value[key]
        if (not isinstance(row, list) or len(row) != 4 or row[2:] != [mode,group]
                or any(type(item) is not int for item in row)):
            raise ValueError('Invalid captured worker directory')
        identity(row[:2])
    identity(value['control'])
    identity(value['prepared'])
    opaque(value['sha256'],64)


def unit(value, namespace, receipt):
    if not isinstance(value, dict) or set(value) != {'native','mount','cgroup','entry'}:
        raise ValueError('Invalid captured declared unit')
    validate_unit(value['native'],'bus')
    if (value['native']['namespace'] != namespace
            or value['native']['runtime'] != [receipt['runtime'][:2]]):
        raise ValueError('The worker unit has another namespace/runtime')
    for key in ('mount','cgroup','entry'):
        identity(value[key])


def declaration(value):
    if not isinstance(value, dict) or set(value) != {'text','sha256','exe','recover_exe'}:
        raise ValueError('Invalid captured worker declaration')
    parsed = Declaration.parse(value['text'])
    opaque(value['sha256'],64)
    if hashlib.sha256(value['text'].encode('utf-8')).hexdigest() != value['sha256']:
        raise ValueError('The captured declaration digest changed')
    identity(value['exe'])
    identity(value['recover_exe'])
    return parsed


def source(value):
    if not isinstance(value, dict) or set(value) != {'bus','sender','owner','epoch','pid','start'}:
        raise ValueError('Invalid original worker bus/lifetime')
    opaque(value['bus'])
    opaque(value['epoch'])
    unique_name(value['sender'])
    unique_name(value['owner'])
    process(value)


def validate_worker(value, uid, gid, namespace):
    fields = {'version','request','source','origin','declaration','phase','files','unit','endpoint','recovery'}
    if (not isinstance(value, dict) or set(value) != fields or type(value['version']) is not int
            or value['version'] != 1 or value['phase'] is not None and not isinstance(value['phase'],str)
            or value['phase'] not in PHASES):
        raise ValueError('Invalid desktop worker receipt')
    opaque(value['request'])
    source(value['source'])
    origin(value['origin'],uid,namespace)
    parsed = declaration(value['declaration'])
    receipt, native = value['files'], value['unit']
    if receipt is None:
        if (value['phase'] != 'files-create' or native is not None or value['endpoint'] is not None
                or value['recovery'] is not None):
            raise ValueError('The worker lost its file acknowledgement')
        return parsed
    files(receipt,gid)
    if value['phase'] == 'files-create':
        raise ValueError('Ambiguous worker file creation acknowledgement')
    if native is None:
        if value['phase'] not in {'unit-start','retire','retained'} | REMOVING:
            raise ValueError('The worker lost its unit acknowledgement')
    else:
        unit(native,namespace,receipt)
        if native['native']['exe'] != value['declaration']['exe']:
            raise ValueError('The worker executable differs from its captured package')
        if value['phase'] == 'unit-start':
            raise ValueError('Ambiguous worker unit creation acknowledgement')
    if value['endpoint'] is not None:
        identity(value['endpoint'])
    if value['phase'] is None and (native is None or value['endpoint'] is None):
        raise ValueError('An active worker requires its published native endpoint')
    recovery = value['recovery']
    if recovery is not None:
        if not isinstance(recovery,dict) or set(recovery) != {'id','phase','files','unit'}:
            raise ValueError('Invalid captured worker recovery')
        opaque(recovery['id'])
        if not isinstance(recovery['phase'],str) or recovery['phase'] not in {'files-create','unit-start','ready-wait','retire','unit-stop','retained'} | REMOVING:
            raise ValueError('Invalid worker recovery transition')
        if recovery['files'] is not None:
            files(recovery['files'],gid)
            if recovery['files']['state'] != receipt['state'] or recovery['phase'] == 'files-create':
                raise ValueError('The recovery has another state or ambiguous acknowledgement')
        elif recovery['phase'] != 'files-create' or recovery['unit'] is not None:
            raise ValueError('The recovery lost its file acknowledgement')
        if recovery['unit'] is not None:
            unit(recovery['unit'],namespace,recovery['files'])
            if (recovery['phase'] == 'unit-start'
                    or recovery['unit']['native']['exe'] != value['declaration']['recover_exe']):
                raise ValueError('The recovery executable acknowledgement changed')
    return parsed


def validate_workers(value, uid, gid, namespace):
    if not isinstance(value, dict) or not 1 <= len(value) <= 16:
        raise ValueError('Invalid desktop worker collection')
    declarations = []
    for instance, receipt in value.items():
        opaque(instance)
        parsed = validate_worker(receipt,uid,gid,namespace)
        if receipt['recovery'] is not None and receipt['recovery']['id'] == instance:
            raise ValueError('Recovery needs a fresh worker identity')
        if any(parsed.resources.conflicts(other.resources) for other in declarations):
            raise ValueError('Desktop worker resource claims overlap')
        declarations.append(parsed)
