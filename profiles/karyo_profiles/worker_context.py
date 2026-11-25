# SPDX-License-Identifier: GPL-3.0-or-later
"""Bind a pinned direct-bus process to an already owned desktop context."""
from dataclasses import dataclass
from pathlib import Path
import re

from .network_info import ready, resolver
from .services import host_controller
from .worker_origin import node


@dataclass(frozen=True)
class WorkerContext:
    scope: object
    origin: object
    desktop_token: str
    manager_invocation: str
    manager_start: int
    resolver_node: tuple
    resolver_bus_node: tuple
    application_scope: str
    application_node: tuple

    @classmethod
    def capture(cls, origin, registry):
        host_controller()
        if not registry.store._locked:
            raise PermissionError('Worker binding requires the profile owner configuration lock')
        process = origin.verify()
        scopes = list(registry.profiles.values()) + list(registry.main.entries.values())
        matches = [scope for scope in scopes if not scope.closed
                   and scope.manager.account.uid == process.uid]
        if len(matches) != 1:
            raise PermissionError('The application has no unique captured desktop')
        scope = matches[0]
        ready(scope)
        manager = scope.manager.identity
        prefix = manager.cgroup.removesuffix('/init.scope') + '/app.slice/'
        application = process.cgroup.removeprefix(prefix)
        # Retirement must never stop a shell/login/user-manager scope. The
        # package launcher must supply an independently owned app service/scope.
        if (not process.cgroup.startswith(prefix) or '/' in application
                or re.fullmatch(r'app-(?:[A-Za-z0-9_.@-]|\\x[0-9a-fA-F]{2})+\.(?:service|scope)', application) is None):
            raise PermissionError('The worker GUI needs an independent application scope')
        current_resolver = resolver(scope)
        bus_path = scope.dns.files.plan.bus_address.removeprefix('unix:path=')
        value = cls(scope, origin, scope.token, manager.invocation, manager.start,
                    tuple(current_resolver['directory']), node(bus_path),
                    process.cgroup, process.cgroup_node)
        value.verify()
        return value

    def verify(self):
        scope = self.scope
        host_controller()
        if not scope.network.network.store._locked:
            raise PermissionError('Worker context verification requires the owner lock')
        process = self.origin.verify()
        ready(scope)
        manager = scope.manager.identity
        account = scope.manager.account
        if (scope.closed or scope.token != self.desktop_token or process.uid != account.uid
                or manager.invocation != self.manager_invocation or manager.start != self.manager_start
                or process.network != scope.network.network.identity
                or process.user != node('/proc/1/ns/user')
                or process.cgroup != self.application_scope or process.cgroup_node != self.application_node):
            raise PermissionError('The bound desktop/application lifetime changed')
        login = scope.login() if getattr(scope, 'is_main', False) else scope.graph.identity.login
        active = scope.manager.sessions.get(login.id)
        if (active is None or not active.active or active.locked or active.uid != account.uid
                or active.name != account.name):
            raise PermissionError('The bound desktop is not active and unlocked')
        root = Path('/proc', str(process.pid), 'root')
        current = resolver(scope)
        if (tuple(current['directory']) != self.resolver_node
                or node(root/'run/systemd/resolve') != self.resolver_node
                or node(scope.dns.files.plan.bus_address.removeprefix('unix:path=')) != self.resolver_bus_node):
            raise PermissionError('The private resolver binding changed')
        record = scope.journal.record(scope.token)
        if 'host_network_policy' in record:
            expected = node('/run/dbus/system_bus_socket')
        else:
            expected = (scope.host_bus.scope.socket_identity if scope.host_bus is not None
                        else self.resolver_bus_node)
        if node(root/'run/dbus/system_bus_socket') != expected:
            raise PermissionError('The GUI system-service view changed')
        return process
