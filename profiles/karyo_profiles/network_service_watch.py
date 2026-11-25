# SPDX-License-Identifier: GPL-3.0-or-later
"""Recover one captured failed network worker without restarting its desktop."""
import sys
import time

from .journal_network_service_record import RESTART_PHASES


class NetworkServiceMonitor:
    def __init__(self, *, clock=time.monotonic, report=None):
        self.clock = clock
        self.report = self.log if report is None else report
        self.retry = {}
        self.messages = {}

    @staticmethod
    def log(message):
        print('karyo-profile-host: ' + message, file=sys.stderr, flush=True)

    def state(self, token, message):
        if self.messages.get(token) != message:
            self.messages[token] = message
            self.report('network worker ' + message)

    def poll(self, profiles, store):
        self.retry = {token: deadline for token, deadline in self.retry.items() if token in profiles}
        self.messages = {token: message for token, message in self.messages.items() if token in profiles}
        now = self.clock()
        for token, scope in profiles.items():
            if now < self.retry.get(token, 0):continue
            record = scope.journal.record(token)
            login = record.get('main_login') if getattr(scope, 'is_main', False) is True else record.get('session')
            if login is None or login['pending'] is not None:continue
            field = None
            for candidate, phases in (('network_service', RESTART_PHASES), ('provider', {'restart-start'})):
                observer = getattr(scope, candidate, None)
                if observer is None:continue
                unit = observer.scope.unit
                if (unit.closed or unit.pidfd is None or unit.process.exited(unit.pidfd)
                        or record[candidate]['pending'] in phases):
                    field = candidate
                    break
            if field is None:continue
            # Recheck ordinary login/manager/process anchors under the same
            # configuration lock. Their normal observer refresh performs the
            # journaled replacement. Bound retries and process one per poll.
            self.retry[token] = now + 15
            try:
                with store.locked():scope.verify_consumers()
                self.state(token, 'recovered in its existing desktop context')
            except (OSError, RuntimeError, ValueError):
                self.state(token, 'recovery retained; retry deferred')
            break
