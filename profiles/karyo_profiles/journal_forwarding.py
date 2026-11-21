# SPDX-License-Identifier: GPL-3.0-or-later
"""Checkpoint fixed forwarding leases without changing host sysctls."""
from copy import deepcopy
from dataclasses import asdict
import json
import os

from .credentials import LocalAccounts
from .forwarding import ProfileForwarding
from .journal import JournalRetained
from .journal_forwarding_record import physical,validate_forwarding
from .journal_network import JournalNetwork,offline,restore_network
from .logind import Sessions
from .networks import NetworkError
from .underlay import discover_underlay


def firewall_absent(gateway):
    value=json.loads(gateway._nft("-j","list","tables"))
    if not isinstance(value,dict) or not isinstance(value.get("nftables"),list) or any(not isinstance(r,dict) for r in value["nftables"]):
        raise JournalRetained("Invalid native firewall table inventory")
    tables=[]
    for row in value["nftables"]:
        if set(row) not in ({"metainfo"},{"table"}) or not isinstance(next(iter(row.values())),dict):
            raise JournalRetained("Unknown native firewall inventory object")
        if "table" in row:tables.append(row["table"])
    return not any(row.get("family")=="inet" and row.get("name")==gateway.name for row in tables)


def apply_proof(gateway,proof):
    gateway.priorities=list(proof["priorities"])
    gateway.route_snapshot=deepcopy(proof["routes"])
    gateway.route_records=[(plan[0],row) for plan,row in zip(gateway.route_plan(),gateway.route_snapshot)]
    gateway.rule_snapshots=deepcopy(proof["rules"])
    firewall=proof["firewall"]
    gateway.firewall_handle=None if firewall is None else firewall["handle"]
    gateway.firewall_snapshot=None if firewall is None else deepcopy(firewall["snapshot"])
    gateway.attempted=True


def receipt(gateway):
    gateway._guard();gateway._verify_owned()
    if gateway.firewall_snapshot is None and not firewall_absent(gateway):
        raise JournalRetained("An unrecorded profile firewall table exists")
    return {"priorities":list(gateway.priorities),"routes":[deepcopy(row) for args,row in gateway.route_records],
            "rules":deepcopy(gateway.rule_snapshots),"firewall":None if gateway.firewall_snapshot is None else
            {"handle":gateway.firewall_handle,"snapshot":deepcopy(gateway.firewall_snapshot)}}


def settle_one_removal(gateway,proof):
    candidate=deepcopy(proof)
    if candidate["rules"]:candidate["rules"].pop()
    elif candidate["firewall"] is not None:candidate["firewall"]=None
    elif candidate["routes"]:candidate["routes"].pop()
    else:raise JournalRetained("An empty forwarding removal has unexpected native state")
    apply_proof(gateway,candidate)
    # At most the next fixed teardown step may have happened before its journal
    # ACK. Exact remaining native rows and nft identity/absence must still match.
    if receipt(gateway)!=candidate:raise JournalRetained("Interrupted forwarding removal changed its receipt")
    return candidate


class JournalForwarding:
    def __init__(self,journal,network,gateway):
        self.journal,self.network,self.gateway=journal,network,gateway
        self.token=network.token

    def capture(self):
        before=self.journal.record(self.token)
        self.journal.change(before,dict(before,forwarding=dict(before["forwarding"],proof=receipt(self.gateway))))

    @classmethod
    def start(cls,journal,network,selected=None):
        before=journal.record(network.token)
        if "forwarding" in before or "dns" in before or "session" in before or before["pending"] is not None:
            raise JournalRetained("Forwarding startup requires its acknowledged empty network")
        selected=discover_underlay(network.network.command) if selected is None else selected
        value={"physical":asdict(selected),"pending":"create","proof":None}
        journal.change(before,dict(before,forwarding=value))
        gateway=ProfileForwarding(network.network,selected)
        scope=cls(journal,network,gateway)
        try:
            gateway.create()
            before=journal.record(network.token)
            journal.change(before,dict(before,forwarding=dict(value,pending=None,proof=receipt(gateway))))
            return scope
        except BaseException:
            if not gateway.attempted or gateway.closed:
                current=journal.record(network.token);updated=dict(current);updated.pop("forwarding")
                journal.change(current,updated)
            raise

    @classmethod
    def restore(cls,journal,network):
        record=journal.record(network.token);value=record["forwarding"]
        validate_forwarding(value,network.network.spec)
        if value["pending"]=="create" or value["proof"] is None:
            raise JournalRetained("Unacknowledged forwarding creation requires review")
        gateway=ProfileForwarding(network.network,physical(value["physical"]))
        apply_proof(gateway,value["proof"])
        scope=cls(journal,network,gateway)
        if value["pending"] is None:gateway.verify()
        try:
            if receipt(gateway)!=value["proof"]:raise JournalRetained("The native forwarding receipt changed")
        except (NetworkError,JournalRetained):
            if value["pending"]!="remove":raise
            settled=settle_one_removal(gateway,value["proof"])
            journal.change(record,dict(record,forwarding=dict(value,proof=settled)))
        return scope

    def verify(self):
        value=self.journal.record(self.token)["forwarding"]
        if value["pending"] is not None or value["proof"]!=receipt(self.gateway):
            raise JournalRetained("The forwarding journal is not an acknowledged live lease")
        self.gateway.verify()

    def close(self):
        before=self.journal.record(self.token)
        if "dns" in before or "session" in before:
            raise JournalRetained("Profile consumers and DNS must close before forwarding")
        value=before["forwarding"]
        if value["pending"] not in {None,"remove"} or value["proof"]!=receipt(self.gateway):
            raise JournalRetained("The forwarding cleanup receipt changed")
        self.journal.change(before,dict(before,forwarding=dict(value,pending="remove")))
        self.gateway.close(after=self.capture)
        before=self.journal.record(self.token);updated=dict(before);updated.pop("forwarding")
        self.journal.change(before,updated)


def recover_forwarding(journal,store,*,accounts=None,sessions=None,command=None,exclude=()):
    accounts=LocalAccounts() if accounts is None else accounts
    sessions=Sessions() if sessions is None else sessions
    for record in journal.snapshot()["profiles"]:
        if record["token"] in exclude or "forwarding" not in record:continue
        if "session" in record or "dns" in record:raise JournalRetained("Forwarding still has higher profile owners")
        with store.locked():
            account=accounts.by_uid(record["account"]["uid"])
            offline(account,accounts,sessions)
            if asdict(account)!=record["account"]:raise JournalRetained("The forwarding journal account changed")
            network=restore_network(store,record,command)
            if network is None:raise JournalRetained("Forwarding requires its pinned namespace")
            try:JournalForwarding.restore(journal,JournalNetwork(journal,network)).close()
            finally:
                if network.fd is not None:
                    os.close(network.fd);network.fd=None
