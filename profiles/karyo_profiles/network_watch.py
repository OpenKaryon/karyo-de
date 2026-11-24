# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded physical DNS observation in the host's ordinary polling loop."""
import sys
import time

from .dns import discover_dns
from .dns_refresh import refresh_dns
from .route_refresh import refresh_routes
from .underlay import discover_underlay


def discover(command):
    with command.bounded(3):
        return discover_dns(discover_underlay(command), command)


class DNSRefreshMonitor:
    def __init__(self, *, clock=time.monotonic, read=discover, apply=refresh_dns,
                 routes=refresh_routes, report=None):
        self.clock, self.read, self.apply = clock, read, apply
        self.routes = routes
        self.report = report if report is not None else self.log
        self.next_read = 0
        self.target = None
        self.queue = []
        self.messages = {}

    @staticmethod
    def log(message):
        print("karyo-profile-host: " + message, file=sys.stderr, flush=True)

    def state(self, token, value):
        if self.messages.get(token) != value:
            self.messages[token] = value
            self.report("physical DNS " + value)

    def poll(self, profiles, store):
        live = {token: scope for token, scope in profiles.items()
                if scope.graph is not None and not scope.graph.closed}
        self.messages = {token: value for token, value in self.messages.items() if token in live}
        if not live:
            self.queue = []
            self.target = None
            self.messages.clear()
            return
        now = self.clock()
        if now >= self.next_read:
            self.next_read = now + 15
            try:
                self.target = self.read(next(iter(live.values())).dns.files.network.command)
            except (OSError, RuntimeError, ValueError):
                self.target = None
                for token in live:
                    self.state(token, "discovery unavailable; retry deferred")
                return
            self.queue = list(live)
        if self.target is None or not self.queue:
            return
        token = self.queue.pop(0)
        scope = live.get(token)
        if scope is None:
            return
        routing = self.target.physical.routing_identity != scope.dns.files.plan.seed.physical.routing_identity
        if routing and scope.forwarding is None:
            self.state(token, "routing changed; route reconciliation required")
            return
        try:
            with store.locked(), scope.dns.files.network.command.bounded(3):
                record = scope.journal.record(token)
                if routing or 'refresh' in record.get('forwarding',{}):
                    changed = self.routes(scope.forwarding,scope.dns,self.target)
                else:
                    if "refresh" in record["dns"]:
                        self.apply(scope.dns, forwarding=scope.forwarding)
                    changed = self.apply(scope.dns, self.target, forwarding=scope.forwarding)
            self.state(token, "updated" if changed else "current")
        except (OSError, RuntimeError, ValueError):
            self.state(token, "update retained; retry deferred")
