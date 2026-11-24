# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh only the owned private physical DNS link, retaining live VPN links."""
from dataclasses import asdict, replace
import ipaddress
import json

from .dns import DNSSeed, LINK_PATH, _reply
from .journal_dns_record import seed
from .networks import NetworkError


def encoded(value):
    return json.loads(json.dumps(asdict(value)))


def wire(value):
    return {
        "DNS": [[2, list(ipaddress.IPv4Address(server).packed)] for server in value.servers],
        "Domains": [[domain, False] for domain in value.search],
    }


def validate_refresh(value, original, *, routing=False):
    if (not isinstance(value, dict) or set(value) != {"target", "step"}
            or type(value["step"]) is not int or not 0 <= value["step"] <= 2):
        raise ValueError("Invalid private DNS refresh intent")
    target = seed(value["target"])
    if not routing and target.physical.routing_identity != original.physical.routing_identity:
        raise ValueError("DNS refresh cannot change physical routing")
    return target


class PrivateLink:
    def __init__(self, dns):
        self.dns = dns
        files, network = dns.files, dns.files.network
        dns.services.verify()
        self.command = network.command
        self.prefix = ("/usr/bin/busctl", "--address=" + files.plan.bus_address,
                       "--json=short", "--timeout=1s")
        owner = _reply(self.command.run(*self.prefix, "call", "org.freedesktop.DBus",
            "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID",
            "s", "org.freedesktop.resolve1"), "u")
        if owner != [dns.services.resolver.identity.pid]:
            raise NetworkError("Private DNS refresh requires the captured bus owner")
        link = network._inventory(inside=True).get("eth0")
        if link is None or type(link.get("ifindex")) is not int:
            raise NetworkError("The owned physical DNS link is unavailable")
        path = _reply(self.command.run(*self.prefix, "call", "org.freedesktop.resolve1",
            "/org/freedesktop/resolve1", "org.freedesktop.resolve1.Manager", "GetLink",
            "i", str(link["ifindex"])), "o")
        if (not isinstance(path, list) or len(path) != 1 or not isinstance(path[0], str)
                or LINK_PATH.fullmatch(path[0]) is None):
            raise NetworkError("Invalid owned physical DNS link object")
        self.path = path[0]

    def read(self, name):
        self.dns.services.verify()
        signature = {"DNS": "a(iay)", "Domains": "a(sb)", "DefaultRoute": "b"}[name]
        return _reply(self.command.run(*self.prefix, "get-property",
            "org.freedesktop.resolve1", self.path, "org.freedesktop.resolve1.Link", name), signature)

    def write(self, name, target):
        self.dns.services.verify()
        values = wire(target)[name]
        arguments = [str(len(values))]
        if name == "DNS":
            for family, address in values:
                arguments.extend((str(family), str(len(address)), *map(str, address)))
        else:
            for domain, routing in values:
                arguments.extend((domain, "true" if routing else "false"))
        self.command.run(*self.prefix, "call", "org.freedesktop.resolve1", self.path,
            "org.freedesktop.resolve1.Link", "Set" + name,
            "a(iay)" if name == "DNS" else "a(sb)", *arguments)


def refresh_dns(dns, target=None, *, forwarding=None, link_factory=PrivateLink):
    """Apply or resume one journaled DNS-only update under the configuration lock.

    A caller must reconcile actual address/gateway changes separately. No host
    resolver, runtime directory, namespace, resolver process or VPN link is replaced.
    """
    dns.files._guard()
    record = dns.journal.record(dns.token)
    value = record["dns"]
    carrier = record.get('forwarding', {}).get('refresh')
    original = seed(value["seed"])
    if value["pending"] is not None or dns.files.plan.seed != original:
        raise NetworkError("DNS refresh requires its acknowledged resolver configuration")
    if "forwarding" in record:
        if forwarding is None or record["forwarding"]["pending"] is not None:
            raise NetworkError("DNS refresh requires its current forwarding owner")
        forwarding.verify()
    dns.services.verify()
    intent = value.get("refresh")
    if intent is not None:
        desired = validate_refresh(intent, original, routing=carrier is not None)
        if target is not None and target != desired:
            raise NetworkError("Finish the captured DNS update before selecting another seed")
    else:
        if not isinstance(target, DNSSeed):
            raise ValueError("DNS refresh requires a discovered physical seed")
        intent = {"target": encoded(target), "step": 0}
        desired = validate_refresh(intent, original, routing=carrier is not None)
        if carrier is not None and intent['target'] != carrier['target']:
            raise NetworkError('The DNS target differs from the owned carrier transition')
        if desired == original:
            return False
    link = link_factory(dns)
    old, new = wire(original), wire(desired)
    if link.read("DefaultRoute") is not True:
        raise NetworkError("The owned physical DNS route setting changed")
    # Each acknowledged property must remain at its captured value. The next
    # property alone may already equal the target after an unacknowledged call.
    names = ("DNS", "Domains")
    for index, name in enumerate(names):
        current = link.read(name)
        expected = new[name] if index < intent["step"] else old[name]
        if current != expected and not (index == intent["step"] and current == new[name]):
            raise NetworkError("The owned physical DNS property changed")
    if "refresh" not in value:
        dns.update(refresh=intent)
    for index in range(intent["step"], len(names)):
        name = names[index]
        if link.read(name) != new[name]:
            link.write(name, desired)
        if link.read(name) != new[name]:
            raise NetworkError("Private DNS refresh was not acknowledged")
        intent = dict(intent, step=index + 1)
        dns.update(refresh=intent)
    dns.services.verify()
    if link.read("DefaultRoute") is not True:
        raise NetworkError("The physical DNS route setting changed during refresh")
    record = dns.journal.record(dns.token)
    updated_dns = dict(record["dns"], seed=encoded(desired))
    updated_dns.pop("refresh")
    updated = dict(record, dns=updated_dns)
    if forwarding is not None and carrier is None:
        updated["forwarding"] = dict(record["forwarding"], physical=encoded(desired)["physical"])
    dns.journal.change(record, updated)
    dns.files.plan = replace(dns.files.plan, seed=desired)
    if forwarding is not None and carrier is None:
        forwarding.gateway.physical = desired.physical
    return True


def cancel_refresh(dns):
    """Retire a DNS-only intent when its consumers have already closed."""
    record = dns.journal.record(dns.token)
    if "refresh" not in record["dns"]:
        return
    if "session" in record or "main_login" in record or "provider" in record:
        raise NetworkError("Live DNS refresh must finish before consumer shutdown")
    value = dict(record["dns"])
    value.pop("refresh")
    dns.journal.change(record, dict(record, dns=value))
