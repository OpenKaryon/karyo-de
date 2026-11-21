#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in native packet probe, restricted to two disposable migration VMs.

This fixture explicitly enables forwarding temporarily and creates a synthetic
blocking Home VPN policy. It restores only captured objects and the original
forwarding value. It never installs or configures a real tunnel or user profile.
"""
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import sys

sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.forwarding import ProfileForwarding, _canonical, _table
from karyo_profiles.networks import NetworkCommand, NetworkError, create_network
from karyo_profiles.storage import ConfigStore
from karyo_profiles.underlay import discover_underlay

QUERY = r"""import json,secrets,socket,struct,sys
ident=secrets.randbelow(65536)
packet=struct.pack('!6H',ident,256,1,0,0,0)+b'\x07example\x03org\0'+struct.pack('!HH',1,1)
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as connection:
 connection.settimeout(1)
 connection.connect((sys.argv[1],53))
 for attempt in range(1,4):
  connection.send(packet)
  try:
   reply=connection.recv(4096)
   break
  except TimeoutError:
   if attempt==3:raise
header=struct.unpack('!6H',reply[:12])
assert header[0]==ident and header[1]&32768 and not header[1]&15 and header[3]>0
print(json.dumps({'rcode':header[1]&15,'answers':header[3],'attempts':attempt}))
"""

TCP = "import socket; connection=socket.create_connection(('1.1.1.1',443),timeout=4); print(connection.getsockname()[0]); connection.close()"


def require(value, message):
    if not value:
        raise RuntimeError(message)


def snapshot(command):
    def native(*args):
        return json.loads(command.run("/usr/sbin/ip", "-j", "-N", *args))
    resolver = Path("/etc/resolv.conf")
    nft = json.loads(command.run("/usr/sbin/nft", "-j", "list", "ruleset"))["nftables"]
    return {"links": [{key: row.get(key) for key in ("ifindex", "ifname", "ifalias", "link_index", "linkinfo")}
                      for row in native("-d", "link", "show")],
            "routes": native("route", "show", "table", "all"), "rules": native("-4", "rule", "show"),
            "namespaces": command.run("/usr/sbin/ip", "netns", "list"),
            "firewall": [row for row in nft if "metainfo" not in row],
            "forward": Path("/proc/sys/net/ipv4/ip_forward").read_text(),
            "ipv4_conf": {str(p): p.read_text() for p in Path("/proc/sys/net/ipv4/conf").glob("*/*")},
            "ipv6": Path("/proc/sys/net/ipv6/conf/all/disable_ipv6").read_text(),
            "resolver_target": str(resolver.resolve()),
            "resolver_sha256": hashlib.sha256(resolver.read_bytes()).hexdigest()}


class SyntheticVPN:
    def __init__(self, command, physical):
        self.command, self.physical = command, physical
        self.table, self.priority = 59000, 100
        self.records, self.expected = [], []
        self.rule = None
        self.uncertain = False

    def ip(self, *args):
        return self.command.run("/usr/sbin/ip", "-4", *args)

    def routes(self):
        return [row for row in json.loads(self.ip("-j", "-N", "route", "show", "table", "all"))
                if _table(row.get("table", 254)) == self.table]

    def rules(self):
        return [row for row in json.loads(self.ip("-j", "-N", "rule", "show"))
                if row.get("priority") == self.priority or ("table" in row and _table(row["table"]) == self.table)]

    def create(self):
        require(not self.routes() and not self.rules(), "Synthetic VPN fixture slots are occupied")
        for args in (("unreachable", "default", "metric", "32767"),
                     (self.physical.subnet, "dev", self.physical.interface, "scope", "link"),
                     ("unreachable", self.physical.gateway + "/32", "metric", "1")):
            self.uncertain = True
            self.ip("route", "add", *args, "table", str(self.table), "proto", "static")
            current = self.routes()
            require(len(current) == len(self.expected) + 1 and all(row in current for row in self.expected),
                    "Synthetic VPN route identity changed")
            self.records.append(args)
            self.expected = current
            self.uncertain = False
        self.uncertain = True
        self.ip("rule", "add", "priority", str(self.priority), "table", str(self.table))
        rows = self.rules()
        require(rows == [{"priority": 100, "src": "all", "table": "59000"}], "Synthetic VPN rule changed")
        self.rule = rows[0]
        self.uncertain = False

    def close(self):
        require(not self.uncertain and _canonical(self.routes()) == _canonical(self.expected),
                "Retained synthetic VPN fixture routes")
        require(self.rules() == ([] if self.rule is None else [self.rule]), "Retained synthetic VPN rule")
        if self.rule is not None:
            self.ip("rule", "delete", "priority", str(self.priority), "table", str(self.table))
            self.rule = None
        for args in reversed(self.records):
            self.ip("route", "delete", *args, "table", str(self.table), "proto", "static")
        require(not self.routes() and not self.rules(), "Synthetic VPN fixture cleanup incomplete")


def outage_probe(gateway):
    args, row = next((args, row) for args, row in gateway.route_records if "via" in args)
    gateway.verify()
    expected = list(gateway.route_snapshot)
    expected.remove(row)
    gateway._ip("route", "delete", *args, "table", str(gateway.table), "proto", "static")
    try:
        require(_canonical(gateway._routes()) == _canonical(expected), "Unknown route changed during outage")
        try:
            gateway._ip("-j", "route", "get", "1.1.1.1", "from", gateway.network.spec.guest,
                        "iif", gateway.network.spec.interface)
        except NetworkError:
            pass
        else:
            raise RuntimeError("Missing physical default fell through the unreachable fallback")
    finally:
        require(_canonical(gateway._routes()) == _canonical(expected), "Cannot restore a changed outage fixture")
        gateway._ip("route", "add", *args, "table", str(gateway.table), "proto", "static")
    gateway.verify()


def main():
    require(os.getuid() == os.geteuid() == 0 and len(sys.argv) == 1, "Use root with no arguments")
    require(socket.gethostname() in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"},
            "Only the disposable migration VMs are supported")
    def interrupted(number, frame):
        raise RuntimeError("Native forwarding fixture interrupted")
    for number in (signal.SIGTERM, signal.SIGHUP, signal.SIGALRM):
        signal.signal(number, interrupted)
    signal.alarm(120)
    command = NetworkCommand()
    before = snapshot(command)
    require(before["forward"].strip() == "0", "Fixture expects initially disabled host forwarding")
    require(all(v.strip() == "0" for p,v in before["ipv4_conf"].items() if p.endswith("/forwarding")),
            "Fixture requires consistent initial per-interface forwarding")
    physical = discover_underlay(command)
    store = ConfigStore()
    vpn = SyntheticVPN(command, physical)
    networks, gateways = [], []
    evidence = {"physical": physical.__dict__}
    try:
        command.run("/usr/sbin/sysctl", "-qw", "net.ipv4.ip_forward=1")
        vpn.create()
        try:
            command.run("/usr/sbin/ip", "-4", "route", "get", "1.1.1.1")
        except NetworkError:
            evidence["home_policy_blocked"] = True
        else:
            raise RuntimeError("Synthetic Home VPN did not block the Home route")
        try:
            command.run("/usr/bin/python3", "-I", "-c", QUERY, physical.gateway)
        except NetworkError:
            evidence["home_dns_blocked"] = True
        else:
            raise RuntimeError("Synthetic Home VPN did not block Home's direct DNS request")
        with store.locked():
            evidence["profiles"] = []
            for uid in (1002, 1003):
                network = create_network(store, uid)
                networks.append(network)
                gateway = ProfileForwarding(network, physical)
                gateways.append(gateway)
                gateway.create()
                outgoing = json.loads(gateway._ip("-j", "route", "get", "1.1.1.1", "from", network.spec.guest,
                                                 "iif", network.spec.interface))[0]
                returning = json.loads(gateway._ip("-j", "route", "get", network.spec.guest,
                                                  "from", physical.gateway, "iif", physical.interface))[0]
                require(outgoing.get("dev") == physical.interface and _table(outgoing.get("table")) == gateway.table,
                        "Outgoing profile route follows Home policy")
                require(returning.get("dev") == network.spec.interface and _table(returning.get("table")) == gateway.table,
                        "Returning profile route follows Home policy")
                reply = json.loads(command.run("/usr/bin/nsenter", "--net=" + network.path, "--",
                                              "/usr/bin/python3", "-I", "-c", QUERY, physical.gateway))
                tcp_source = command.run("/usr/bin/nsenter", "--net=" + network.path, "--",
                                         "/usr/bin/python3", "-I", "-c", TCP).strip()
                require(tcp_source == network.spec.guest, "TCP probe did not originate in the private namespace")
                outage_probe(gateway)
                evidence["profiles"].append({"namespace": network.name, "table": gateway.table,
                                              "priorities": gateway.priorities, "dns_packet": reply, "external_tcp": True,
                                              "unreachable_fallback": True})
    finally:
        errors = []
        with store.locked():
            for gateway in reversed(gateways):
                if not gateway.closed:
                    try:
                        gateway.close()
                    except Exception as error:
                        print("RETAINED forwarding", gateway.name, gateway.uncertain, str(error), flush=True)
                        errors.append(str(error))
            for network in reversed(networks):
                if any(g.network is network and not g.closed for g in gateways):
                    errors.append("Namespace retained with forwarding: " + network.name)
                    continue
                if not network.closed:
                    try:
                        network.close()
                    except Exception as error:
                        errors.append(str(error))
        try:
            vpn.close()
        except Exception as error:
            errors.append(str(error))
        command.run("/usr/sbin/sysctl", "-qw", "net.ipv4.ip_forward=0")
        signal.alarm(0)
        require(not errors, "Retained native QA resources: " + "; ".join(errors))
    after = snapshot(command)
    if after != before:
        print(json.dumps({key: {"before": before[key], "after": after[key]} for key in before if before[key] != after[key]},
                         indent=2), flush=True)
        raise RuntimeError("Host networking or resolver changed after native forwarding cleanup")
    evidence["host_network_restored"] = True
    print(json.dumps(evidence, indent=2))
    print("PASS installed physical forwarding; Home policy blocked; two profiles exchange NATed DNS/TCP packets")


if __name__ == "__main__":
    main()
