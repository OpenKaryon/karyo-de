# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe and retire the optional network worker beside graphical ownership."""
from .journal_network_service import JournalNetworkService
from .provider_state import STATE_ROOT
from .journal_network_service_record import RESTART_PHASES


def restore_network_service(journal, dns, *, state_root=STATE_ROOT):
    if 'network_service' not in journal.record(dns.token):return None
    return JournalNetworkService.restore(journal, dns, state_root=state_root)


def refresh_network_service(journal, dns, observer):
    if observer is None:return None
    unit = observer.scope.unit
    pending = journal.record(dns.token)['network_service']['pending']
    if (not unit.closed and unit.pidfd is not None and not unit.process.exited(unit.pidfd)
            and pending not in RESTART_PHASES):return observer
    root = observer.scope.state.path.parent.parent
    observer.release_handles()
    current = restore_network_service(journal, dns, state_root=root)
    if current is None:return None
    try:
        record = journal.record(dns.token)
        logins = [record[key] for key in ('session', 'main_login') if key in record]
        if logins and all(login['pending'] is None for login in logins):
            current.restart()
        return current
    except BaseException:
        current.release_handles()
        raise


def worker_pids(*observers):
    values = set()
    for observer in observers:
        if observer is not None and not observer.scope.unit.closed:
            values.update(observer.scope.unit.consumer_pids())
    return values


def retire_network_service(journal, dns, observer=None):
    if observer is not None:observer.release_handles()
    current = restore_network_service(journal, dns)
    if current is None:return
    try:current.close()
    finally:current.release_handles()
