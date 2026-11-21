# SPDX-License-Identifier: GPL-3.0-or-later
"""Journal and recover only proven network leases before later graph layers exist."""
from dataclasses import asdict
import os
from pathlib import Path
import time

from .credentials import LocalAccounts
from .journal import JournalRetained, validate_account, validate_network
from .logind import Sessions
from .networks import NetworkSpec, ProfileNetwork
from .user_manager import uid_processes


def receipt(network):
    network.verify()
    host = network._inventory()[network.spec.interface]
    peer = network._inventory(inside=True)["eth0"]
    proof = {"namespace": list(network.identity), "indices": list(network.indices),
             "macs": [host.get("address"), peer.get("address")]}
    validate_network(proof)
    return proof


def offline(account, accounts, sessions):
    if accounts.by_uid(account.uid) != account or sessions.owners(account.uid) or uid_processes(account.uid):
        raise JournalRetained("The journal desktop account is changed or still has login/process owners")


class JournalNetwork:
    def __init__(self, journal, network):
        self.journal, self.network = journal, network
        self.token = network.spec.token

    @classmethod
    def start(cls, journal, store, account, source, *, accounts=None, sessions=None, command=None):
        if not store._locked:
            raise PermissionError("Journaled network startup requires the configuration lock")
        accounts = LocalAccounts() if accounts is None else accounts
        sessions = Sessions() if sessions is None else sessions
        validate_account(asdict(account))
        if (sessions.get(source.id) != source or not source.active or account.uid == source.uid
                or accounts.by_uid(source.uid).name != source.name
                or account.name not in store.ownership().family(source.name)):
            raise PermissionError("Only a child in the active source desktop's family may start")
        offline(account, accounts, sessions)
        expiry, hashed = accounts.expiry_day(account.name), accounts.password_hash(account.name)
        if ((expiry is not None and expiry <= int(time.time() // 86400))
                or not hashed or hashed.startswith(("!", "*"))):
            raise PermissionError("The journal desktop is locked or expired")
        token = journal.begin(account, source)
        network = ProfileNetwork(store, NetworkSpec(account.uid, token), command)
        expected = journal.record(token)
        try:
            network.create()
            updated = dict(expected, pending=None, network=receipt(network))
            journal.change(expected, updated)
            return cls(journal, network)
        except BaseException:
            # The native layer either made no attempt or acknowledged its own
            # complete rollback. An uncertain creation intent is never erased.
            if not network.attempted or network.closed:
                journal.change(expected, None)
            raise

    def close(self):
        if self.network.closed:
            return
        expected = self.journal.record(self.token)
        if expected["pending"] is not None or expected["network"] != receipt(self.network):
            raise JournalRetained("The captured network journal receipt changed")
        closing = dict(expected, pending="remove")
        self.journal.change(expected, closing)
        self.network.close()
        self.journal.change(closing, None)


def namespace_processes(identity):
    for path in Path("/proc").iterdir():
        if not path.name.isdecimal():
            continue
        try:
            info = os.stat(path / "ns/net")
            if (info.st_dev, info.st_ino) == identity:
                return True
        except (FileNotFoundError, ProcessLookupError):
            pass
    return False


def restore_network(store, record, command=None):
    """Pin a receipt, never acquire a replacement just because its name matches."""
    account = validate_account(record["account"])
    proof = record["network"]
    validate_network(proof)
    network = ProfileNetwork(store, NetworkSpec(account.uid, record["token"]), command)
    try:
        network.fd, network.identity = network._pin()
    except FileNotFoundError:
        links = network._inventory()
        if (record["pending"] != "remove" or network.spec.interface in links
                or any(row["ifindex"] == proof["indices"][0] for row in links.values())
                or namespace_processes(tuple(proof["namespace"]))):
            raise JournalRetained("The journal namespace disappeared without proven cleanup")
        return None  # An acknowledged removal intent may already be complete.
    try:
        if network.identity != tuple(proof["namespace"]):
            raise JournalRetained("The journal namespace was replaced")
        network.attempted = network.namespace_created = True
        links = network._inventory()
        inside = network._inventory(inside=True)
        if network.spec.interface not in links:
            if (record["pending"] != "remove" or set(inside) != {"lo"}
                    or any(row["ifindex"] == proof["indices"][0] for row in links.values())):
                raise JournalRetained("The journal link pair changed without proven cleanup")
            network._empty()
            return network  # Only the pinned known empty namespace remains.
        network.indices = tuple(proof["indices"])
        network.tagged = {"host", "peer"}
        network.pair_attempted = True
        if receipt(network) != proof:
            raise JournalRetained("The journal link pair or address identity changed")
        network._empty()
        return network
    except BaseException:
        os.close(network.fd)
        network.fd = None
        raise


def recover_networks(journal, store, *, accounts=None, sessions=None, command=None):
    """Startup gate for the current network-only journal version.

    Later graph receipts require their own typed capture/recovery; an unknown
    schema or unacknowledged creation is retained before any new startup.
    """
    accounts = LocalAccounts() if accounts is None else accounts
    sessions = Sessions() if sessions is None else sessions
    records = journal.snapshot()["profiles"]
    if any(record["pending"] == "create" or record["network"] is None for record in records):
        raise JournalRetained("An unacknowledged profile creation requires administrator review")
    for expected in records:
        with store.locked():
            account = validate_account(expected["account"])
            offline(account, accounts, sessions)
            journal.guard()
            network = restore_network(store, expected, command)
            try:
                closing = dict(expected, pending="remove")
                if expected != closing:
                    journal.change(expected, closing)
                if network is not None:
                    network.close()
                journal.change(closing, None)
            finally:
                if network is not None and network.fd is not None:
                    os.close(network.fd)
                    network.fd = None
