# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only network inspection from captured owners, not provider authority."""
from contextlib import ExitStack, contextmanager
from dataclasses import asdict
import fcntl
import os
from pathlib import Path
import stat
import time

from .credentials import LocalAccounts
from .networks import NetworkError
from .services import host_controller
from .storage import trusted_directory


def identity(fd):
    info = os.fstat(fd)
    return info.st_dev, info.st_ino


@contextmanager
def namespace(*, path=None, descriptor=None, expected=None):
    fd = os.dup(descriptor) if descriptor is not None else os.open(path, os.O_RDONLY | os.O_CLOEXEC)
    try:
        os.set_inheritable(fd, False)
        if fcntl.ioctl(fd, 0xb703) != 0x40000000:  # NS_GET_NSTYPE / CLONE_NEWNET
            raise NetworkError('The captured context is not a network namespace')
        captured = identity(fd)
        if expected is not None and captured != expected:
            raise NetworkError('The captured network namespace changed')
        yield captured
    finally:
        os.close(fd)


def ready(scope):
    if scope.graph is None or scope.graph.closed:
        raise NetworkError('The captured desktop is not running')
    record = scope.journal.record(scope.token)
    for role in ('dns', 'forwarding', 'session', 'provider'):
        value = record.get(role, {})
        if value.get('pending') is not None or 'refresh' in value:
            raise NetworkError('A desktop transition is still in progress')
    if record.get('pending') is not None or scope.forwarding is None:
        raise NetworkError('The desktop network has no acknowledged forwarding owner')
    scope.verify_consumers()


def resolver(scope):
    """Pin the existing private directory without traversing application data."""
    files = scope.dns.files
    path = Path(files.plan.runtime_paths[0])
    parent = trusted_directory(path.parent)
    try:
        fd = os.open(path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                     dir_fd=parent)
    finally:
        os.close(parent)
    try:
        info = os.fstat(fd)
        captured = list(identity(fd))
        expected = scope.journal.record(scope.token)['dns']['resolver']['runtime'][0]
        if (captured != expected or info.st_uid != files.plan.service_uid
                or info.st_gid != files.plan.gid or stat.S_IMODE(info.st_mode) != 0o750):
            raise NetworkError('The private resolver directory identity changed')
        unit = scope.dns.services.resolver.identity
        return {'directory': captured, 'namespace': list(scope.network.network.identity),
                'bus': files.plan.bus_address, 'pid': unit.pid, 'start': unit.start,
                'invocation': unit.invocation}
    finally:
        os.close(fd)


def physical_peer(scope):
    network = scope.network.network
    return {'uid': scope.manager.account.uid, 'profile_token': scope.token,
            'namespace': list(network.identity),
            'host_link': {'name': network.spec.interface, 'index': network.indices[0]},
            'profile_address': network.spec.guest,
            'physical': asdict(scope.forwarding.gateway.physical)}


def describe(peer, registry, store, accounts=None):
    """Inspect only the active caller while holding the host configuration lock.

    The returned JSON is deliberately not a lease and exports no descriptors.
    Provider startup, registration, revocation and cleanup require a separate
    lifecycle protocol; this diagnostic must not authorize route/DNS mutation.
    """
    host_controller()
    if registry.store is not store or not store._locked:
        raise PermissionError('Network inspection requires the owner configuration lock')
    accounts = LocalAccounts() if accounts is None else accounts
    account = peer.verify()
    ownership = store.ownership()
    family = ownership.family(account.name)
    scopes = []
    for token, scope in registry.profiles.items():
        if scope.manager.account.name in family:
            if token != scope.token or accounts.by_uid(scope.manager.account.uid) != scope.manager.account:
                raise NetworkError('The captured desktop account changed')
            scopes.append(scope)
    selected = [scope for scope in scopes if scope.manager.account == account]
    if ownership.is_child(account.name):
        if len(selected) != 1:
            raise NetworkError('The current desktop has no unique captured network')
        selected = selected[0]
        login = selected.graph.identity.login if selected.graph is not None else None
        if login is None or (login.id, login.uid, login.name, login.vt) != peer.identity.session:
            raise PermissionError('The captured desktop is not the current login')
        observed = [selected]
    else:
        if selected:
            raise NetworkError('A main desktop unexpectedly owns a private profile context')
        selected = None
        observed = sorted(scopes, key=lambda scope: scope.manager.account.uid)
    if len(observed) > 16:
        raise NetworkError('The physical peer inventory exceeds the desktop limit')
    deadline = time.monotonic() + 8
    with ExitStack() as pins:
        for scope in observed:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('Network inspection inventory timed out')
            with scope.network.network.command.bounded(min(remaining, 3)):
                ready(scope)
            pins.enter_context(namespace(descriptor=scope.network.network.fd,
                                         expected=scope.network.network.identity))
        if selected is None:
            current = pins.enter_context(namespace(path='/proc/1/ns/net'))
            private_resolver = None
        else:
            current = selected.network.network.identity
            private_resolver = resolver(selected)
        pins.enter_context(namespace(path='/proc/' + str(peer.identity.pid) + '/ns/net', expected=current))
        result = {'version': 1, 'inspection_only': True,
                  'kind': 'host' if selected is None else 'profile',
                  'uid': account.uid, 'session_id': peer.identity.session[0],
                  'namespace': list(current),
                  'profile_token': None if selected is None else selected.token,
                  'resolver': private_resolver,
                  'physical_peers': [physical_peer(scope) for scope in observed]}
        if peer.verify() != account:
            raise PermissionError('The network inspection caller changed')
        for scope in observed:
            if registry.profiles.get(scope.token) is not scope:
                raise NetworkError('The captured desktop lifetime changed')
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('Network inspection verification timed out')
            with scope.network.network.command.bounded(min(remaining, 3)):
                ready(scope)
        if selected is not None and resolver(selected) != private_resolver:
            raise NetworkError('The private resolver changed during inspection')
        return result
