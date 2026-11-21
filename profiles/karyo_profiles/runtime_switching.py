# SPDX-License-Identifier: GPL-3.0-or-later
"""Public stopped-child startup owned by the host's lifetime registry."""
import os

from .dns import discover_dns, supported_resolver_layout
from .journal import JournalRetained
from .journal_dns import JournalDNS
from .journal_forwarding import JournalForwarding
from .journal_network import JournalNetwork
from .journal_session import JournalSession
from .logind import SessionError
from .model import account_name
from .networks import NetworkCommand
from .runtime_policy import enabled, forwarding_ready
from .switching import DesktopStopped, ExistingSwitcher
from .underlay import discover_underlay


class RuntimeSwitcher(ExistingSwitcher):
    def __init__(self, store, *, registry, accounts=None, sessions=None):
        super().__init__(store, accounts=accounts, sessions=sessions)
        if registry.store is not store:
            raise ValueError("The desktop runtime must share its registry configuration lock")
        self.registry = registry

    def switch(self, caller, target, *, reauthorize=None):
        try:
            return super().switch(caller, target, reauthorize=reauthorize)
        except DesktopStopped:
            return self.start(caller, target, reauthorize=reauthorize)

    def start(self, caller, target, *, reauthorize=None):
        account_name(target)
        with self.store.locked():
            if reauthorize is not None and reauthorize() != caller:
                raise PermissionError("The desktop caller changed before startup")
            if self.accounts.by_uid(caller.uid) != caller:
                raise PermissionError("The authenticated desktop account changed")
            ownership = self.store.ownership()
            if target not in ownership.family(caller.name):
                raise PermissionError("Only a desktop in the caller's family may start")
            if not ownership.is_child(target):
                raise DesktopStopped("A stopped main desktop must be started by the login manager")
            selected = self.accounts.account(target)
            expiry, hashed = self.accounts.expiry_day(target), self.accounts.password_hash(target)
            if ((expiry is not None and expiry <= self.today()) or not hashed or hashed.startswith(("!", "*"))):
                raise PermissionError("The selected desktop account is locked or expired")
            values = self.sessions.all()
            source = self.single(values, caller, active=True)
            if any(value.name == target or value.uid == selected.uid for value in values):
                raise SessionError("The target login changed while preparing startup; retry selection")
            if any(record["account"]["uid"] == selected.uid
                   for record in self.registry.journal.snapshot()["profiles"]):
                raise JournalRetained("The desktop already has a retained runtime receipt")
            if not enabled():
                raise DesktopStopped("Private desktop startup has not been enabled by the administrator")
            if self.store.settings(target).vpn != "none":
                raise RuntimeError("The configured profile VPN requires the separate provider integration")
            forwarding_ready()
            supported_resolver_layout()
            command = NetworkCommand()
            physical = discover_underlay(command)
            seed = discover_dns(physical, command)
            if reauthorize is not None and reauthorize() != caller:
                raise PermissionError("The desktop caller changed before allocation")
            network = dns = scope = None
            try:
                network = JournalNetwork.start(self.registry.journal, self.store, selected, source,
                                               accounts=self.accounts, sessions=self.sessions, command=command)
                JournalForwarding.start(self.registry.journal, network, physical)
                dns = JournalDNS.start(self.registry.journal, network, seed, accounts=self.accounts)
                scope = JournalSession.start(self.registry.journal, dns)
                # Register before activation: a lost caller or failed enter must
                # not orphan a fully captured graphical session's observers.
                self.registry.profiles[scope.token] = scope
                return scope.enter(caller, reauthorize=reauthorize)
            except BaseException:
                if scope is None:
                    # Mutation receipts remain authoritative after any partial
                    # start. Closing observers never erases uncertain resources.
                    if dns is not None:
                        dns.release_handles()
                    if network is not None and network.network.fd is not None:
                        os.close(network.network.fd)
                        network.network.fd = None
                raise
