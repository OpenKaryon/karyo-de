#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual external NetBird daemon acceptance before provider/tunnel activation."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time

if (os.getuid() != 0 or os.geteuid() != 0 or sys.argv[1:] or socket.gethostname() not in
        {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}):
    raise SystemExit("Use only the fixed disposable hosts as root")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import discover_dns
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.logind import Sessions
from karyo_profiles.netbird import NetBirdScope, NetBirdState
from karyo_profiles.storage import ConfigStore
from karyo_profiles.underlay import discover_underlay


def require(value, message):
    if not value: raise RuntimeError(message)


def run(*args):
    return subprocess.check_output(args, text=True, timeout=20).strip()


def snapshot():
    return {key: run(*args) for key, args in {
        "routes": ("/usr/sbin/ip", "-4", "-j", "-N", "route", "show", "table", "all"),
        "rules": ("/usr/sbin/ip", "-4", "-j", "-N", "rule", "show"),
        "nft": ("/usr/sbin/nft", "-j", "list", "ruleset"),
        "netns": ("/usr/sbin/ip", "netns", "list"),
    }.items()} | {"resolver": os.readlink("/etc/resolv.conf"),
                 "resolver_sha": hashlib.sha256(Path("/etc/resolv.conf").read_bytes()).hexdigest()}


def ordinary(name, address):
    result = subprocess.run(["/usr/sbin/runuser", "-u", name, "--", "/usr/bin/netbird",
                             "--daemon-addr", address, "status", "--json"],
                            text=True, capture_output=True, timeout=15)
    return result


store, accounts, sessions = ConfigStore(), LocalAccounts(), Sessions()
source = next(row for row in sessions.all() if row.name == "karyoqa" and row.uid == 1001 and row.active)
require(Path("/proc/sys/net/ipv4/ip_forward").read_text().strip() == "0", "QA forwarding baseline changed")
require(not Path("/var/lib/karyo/profile-vpn/1002").exists(), "Prior provider state requires review")
require(not Path("/etc/systemd/system/netbird.service").exists()
        and not Path("/usr/lib/systemd/system/netbird.service").exists(), "Global NetBird was installed")
original = snapshot()
original_conf = {str(path): path.read_text() for path in Path("/proc/sys/net/ipv4/conf").glob("*/*")}
original_accounts = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"), Path("/etc/shadow"), Path("/etc/group"), Path("/etc/gshadow"),
     *Path("/etc/pam.d").glob("common-*")]}
with RuntimeJournal() as journal:
    require(not journal.snapshot()["profiles"], "Prior resources prevent provider acceptance")
run("/usr/local/libexec/karyo-qa-two-sessions", "prepare")
child = accounts.account("kq-switch-work")
require(child.uid == 1002 and child.gid == 1002, "Unexpected provider QA account")
state_identity = None
try:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("1\n")
    for cycle in (1, 2):
        with RuntimeJournal() as journal, store.locked():
            network = JournalNetwork.start(journal, store, child, source)
            physical = discover_underlay(network.network.command)
            forwarding = JournalForwarding.start(journal, network, physical)
            dns = JournalDNS.start(journal, network, discover_dns(physical, network.network.command))
            provider = NetBirdScope(dns)
            try:
                provider.start()
                current = provider.state.identity
                if state_identity is None: state_identity = current
                else: require(current == state_identity, "Provider state was recreated between runtime leases")
                deadline = time.monotonic() + 20
                while True:
                    status = provider.status()
                    result = ordinary(child.name, provider.address)
                    require(result.returncode == 0, "Work cannot query its private provider socket: " + result.stderr)
                    ordinary_status = json.loads(result.stdout)
                    if status.get("daemonStatus") == "NeedsLogin" and ordinary_status.get("daemonStatus") == "NeedsLogin":
                        break
                    if time.monotonic() >= deadline:
                        break
                    time.sleep(.1)
                # Never publish complete upstream status/config, which may gain
                # profile names, management URLs or keys in later client versions.
                daemon = status.get("daemonStatus")
                require(daemon == "NeedsLogin" and ordinary_status.get("daemonStatus") == daemon,
                        "Provider pre-login state did not settle: root=" + str(daemon) + ", Work=" + str(ordinary_status.get("daemonStatus")))
                denied = ordinary("karyoqa", provider.address)
                require(denied.returncode != 0, "Home unexpectedly controls the Work provider socket")
                process = Path("/proc") / str(provider.unit.identity.pid)
                mount = (process / "root/run/systemd/resolve").stat()
                private = Path("/run", dns.files.plan.runtime_name).stat()
                require((mount.st_dev, mount.st_ino) == (private.st_dev, private.st_ino), "Daemon DNS mount is not private")
                env = (process / "environ").read_bytes().split(bytes([0]))
                require(("DBUS_SYSTEM_BUS_ADDRESS=" + dns.files.plan.bus_address).encode() in env,
                        "Provider uses a foreign system bus")
                caps = next(line.split()[1] for line in (process / "status").read_text().splitlines()
                            if line.startswith("CapBnd:"))
                require(int(caps, 16) & ((1 << 21) | (1 << 16)) == 0, "Provider unexpectedly has SYS_ADMIN/SYS_MODULE")
                print(json.dumps({"cycle": cycle, "actual_daemon": "0.82.1", "status": daemon,
                    "work_socket": True, "home_denied": True, "private_net_dns": True,
                    "state_reused": cycle == 2}), flush=True)
            finally:
                provider.close()
                dns.close()
                forwarding.close()
                network.close()
                dns.release_handles()
            require(not journal.snapshot()["profiles"], "Provider lower-layer receipts remain")
        require(snapshot() == original, "Provider changed the host network/resolver")
finally:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("0\n")
    for name, value in original_conf.items():
        path = Path(name)
        if path.read_text() != value: path.write_text(value)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"], "Retained provider resources; do not remove QA state/account")
    require(not run("/usr/sbin/ip", "netns", "list"), "Private namespace remains")
    if state_identity is not None:
        state = Path("/var/lib/karyo/profile-vpn/1002/netbird")
        info = state.stat()
        require((info.st_dev, info.st_ino) == state_identity, "QA provider state was replaced")
        # Explicit cleanup of the previously absent fixed disposable fixture.
        # Product code deliberately keeps opaque state and never removes it.
        require(json.loads((state / "owner.json").read_text())["account"]["name"] == child.name,
                "QA state ownership stamp changed")
        shutil.rmtree(state)
        state.parent.rmdir()
    run("/usr/local/libexec/karyo-qa-two-sessions", "cleanup")
require(snapshot() == original and sessions.get(source.id) == source, "Original Home state changed")
require({path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}
        == original_accounts, "Account/common PAM restoration failed")
require(not run("/usr/bin/dpkg", "-V", "karyo-profile-accounts", "karyo-profile-host", "karyo-profile-runtime"),
        "Installed DE provider code changed")
require(not run("/usr/bin/dpkg", "--audit"), "Package audit failed")
print("PASS real pre-login NetBird service, private state/socket/DNS and two clean runtime leases", flush=True)
