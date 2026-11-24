# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe and retire the optional network worker beside graphical ownership."""
from .journal_network_service import JournalNetworkService
from .provider_state import STATE_ROOT


def restore_network_service(journal, dns, *, state_root=STATE_ROOT):
    if 'network_service' not in journal.record(dns.token):return None
    return JournalNetworkService.restore(journal, dns, state_root=state_root)


def refresh_network_service(journal, dns, observer):
    if observer is None:return None
    unit = observer.scope.unit
    if unit.closed or unit.pidfd is None or not unit.process.exited(unit.pidfd):return observer
    observer.release_handles()
    return restore_network_service(journal, dns)


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
