#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in disposable-VM probe of the installed owned network primitive.

No desktop account, VPN, DNS or firewall is changed. Never run on an owner VM.
The root supervisor is represented by this process holding both lease objects.
"""
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.networks import NetworkCommand, NetworkError, NetworkRetained, create_network
from karyo_profiles.storage import ConfigStore


def require(value, message):
    if not value:
        raise RuntimeError(message)


def snapshot(command):
    def native(*args):
        return json.loads(command.run("/usr/sbin/ip", "-j", *args))
    links = [{key: row.get(key) for key in ("ifindex", "ifname", "ifalias", "link_index", "linkinfo")}
             for row in native("-d", "link", "show")]
    resolver = Path("/etc/resolv.conf")
    return {"links": links, "routes": native("route", "show", "table", "all"),
            "rules": native("rule", "show"), "namespaces": command.run("/usr/sbin/ip", "netns", "list"),
            "forward": Path("/proc/sys/net/ipv4/ip_forward").read_text(),
            "ipv6": Path("/proc/sys/net/ipv6/conf/all/disable_ipv6").read_text(),
            "resolver_target": str(resolver.resolve()),
            "resolver_sha256": hashlib.sha256(resolver.read_bytes()).hexdigest()}


def main():
    require(os.getuid() == os.geteuid() == 0 and len(sys.argv) == 1, "Use root with no arguments")
    require(socket.gethostname() in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"},
            "Only the two disposable migration VMs are supported")
    command = NetworkCommand()
    before = snapshot(command)
    require(before["forward"].strip() == "0", "This fixture expects unchanged disabled host forwarding")
    store = ConfigStore()
    leases = []
    child = None
    evidence = {}
    try:
        with store.locked():
            first = create_network(store, 1002)
            leases.append(first)
            second = create_network(store, 1003)
            leases.append(second)
            evidence["namespaces"] = [lease.name for lease in leases]
            evidence["identities"] = [lease.identity for lease in leases]
            evidence["link_indices"] = [lease.indices for lease in leases]
            require(first.identity != second.identity and first.fd != second.fd, "Distinct namespaces expected")
            require(all(not os.get_inheritable(lease.fd) for lease in leases), "Namespace descriptors leaked")
            for lease in leases:
                lease.verify()
                command.run("/usr/bin/nsenter", "--net=" + lease.path, "--",
                            "/usr/bin/ping", "-c", "1", "-W", "2", lease.spec.host)
                v6 = json.loads(command.run("/usr/sbin/ip", "-n", lease.name, "-6", "-j", "address"))
                require(all(not row.get("addr_info") for row in v6), "Private IPv6 unexpectedly enabled")
            child = subprocess.Popen(["/usr/bin/nsenter", "--net=" + first.path, "--", "/usr/bin/sleep", "30"],
                                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, close_fds=True,
                                     env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LANG": "C.UTF-8"})
            deadline = time.monotonic() + 3
            while str(child.pid) not in first._ip("netns", "pids", first.name).splitlines():
                require(child.poll() is None and time.monotonic() < deadline, "Owned consumer did not enter")
                time.sleep(0.01)
            try:
                first.close()
            except NetworkRetained:
                first.verify()
                require(child.poll() is None, "Cleanup killed a namespace consumer")
                evidence["live_consumer_refused"] = True
            else:
                raise RuntimeError("Cleanup unexpectedly removed an in-use namespace")
            child.terminate()
            child.wait(timeout=3)
            child = None
            # Simulate changed ownership, prove refusal, then restore only the
            # exact fixture-owned host interface to permit normal teardown.
            command.run("/usr/sbin/ip", "link", "set", "dev", first.spec.interface, "alias", "karyo-qa-replaced")
            try:
                try:
                    first.close()
                except NetworkRetained:
                    require(first._inventory()[first.spec.interface]["ifindex"] == first.indices[0],
                            "Refusal changed the owned interface")
                    evidence["changed_alias_refused"] = True
                else:
                    raise RuntimeError("Cleanup accepted a replaced link ownership alias")
            finally:
                first._verify_namespace()
                row = first._inventory().get(first.spec.interface, {})
                require(row.get("ifindex") == first.indices[0] and row.get("ifalias") == "karyo-qa-replaced",
                        "Cannot restore a changed foreign interface")
                command.run("/usr/sbin/ip", "link", "set", "dev", first.spec.interface, "alias", first.spec.alias)
            first.verify()
            # A second lease for the same UID must refuse the live connected
            # subnet without deleting or adopting either existing namespace.
            try:
                create_network(store, 1002)
            except NetworkError as error:
                require(not isinstance(error, NetworkRetained), "Expected a read-only subnet collision")
                evidence["subnet_collision_refused"] = True
            else:
                raise RuntimeError("Overlapping private desktop subnet was accepted")
    finally:
        if child is not None:
            if child.poll() is None:
                child.terminate()
            child.wait(timeout=3)
        # Each close independently verifies ownership. An uncertain resource
        # is retained; no wildcard, link-name sweep or process killing exists.
        errors = []
        with store.locked():
            for lease in reversed(leases):
                if not lease.closed:
                    try:
                        lease.close()
                    except Exception as error:
                        errors.append(type(error).__name__ + ": " + lease.name)
        require(not errors, "Retained fixture resources: " + ", ".join(errors))
    after = snapshot(command)
    require(after == before, "Host network or resolver changed after owned resource cleanup")
    evidence["host_network_restored"] = True
    print(json.dumps(evidence, indent=2))
    print("PASS installed native profile networks; no forwarding, firewall, DNS or session changes")


if __name__ == "__main__":
    main()
