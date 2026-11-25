# SPDX-License-Identifier: GPL-3.0-or-later
"""Reobserve a captured provider worker, including its fully retired lifetime."""
import os

from .journal import JournalRetained
from .journal_dns import process_gone
from .journal_network_service import NetworkServicePending
from .services import UnitIdentity


def restore_unit(scope, proof):
    unit = scope.unit
    if proof is None:
        if unit.info()['LoadState'] != 'not-found' or os.path.lexists(scope.directory):
            raise JournalRetained('An unknown provider occupies a pending launch identity')
        unit.closed = True
        return
    native = proof['unit']
    unit.identity = UnitIdentity(native['invocation'], native['pid'], '/system.slice/' + unit.name, native['start'])
    unit.executable, unit.attempted = tuple(native['exe']), True
    scope.socket_identity, scope.endpoint_identity = tuple(proof['socket']), tuple(proof['endpoint'])
    rows = unit.info()
    gone = process_gone(native['pid'], native['start'])
    if not gone:
        try:unit.pidfd = unit.process.open(native['pid'])
        except ProcessLookupError:gone = True
        else:gone = unit.process.exited(unit.pidfd)
    if not gone:
        scope.endpoint_contents = scope.endpoint_text()
        return
    if (rows['LoadState'] == 'loaded' and rows['Transient'] == 'yes'
            and rows['InvocationID'] == native['invocation'] and rows['MainPID'] in {'0', str(native['pid'])}
            and rows['ControlGroup'] in {'', unit.identity.cgroup}
            and (rows['ActiveState'] not in {'inactive', 'failed'} or rows['ControlGroup']
                 or os.path.lexists(scope.directory))):
        raise NetworkServicePending('Waiting for the captured provider cgroup to retire')
    if rows['LoadState'] != 'not-found' and (
            rows['Transient'] != 'yes' or rows['InvocationID'] != native['invocation']
            or rows['MainPID'] != '0' or rows['ActiveState'] not in {'inactive', 'failed'} or rows['ControlGroup']):
        raise JournalRetained('The stopped provider worker identity changed')
    if os.path.lexists(scope.directory) or unit.cgroup_pids():
        raise JournalRetained('The stopped provider retained descendants or runtime objects')
    unit.closed = True
    if unit.pidfd is not None:unit.process.close(unit.pidfd);unit.pidfd = None
