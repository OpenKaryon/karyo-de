#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host crash acceptance for an installed pre-login provider."""
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import socket
import subprocess
import sys
import time

if (os.getuid() != 0 or os.geteuid() != 0 or socket.gethostname() not in
        {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}
        or sys.argv[1:] not in ([], ["--controller"])):
    raise SystemExit("Use only the fixed disposable hosts as root")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dispatcher import ADDRESS as SOCKET_ADDRESS, socket_node
from karyo_profiles.dns import discover_dns
from karyo_profiles.journal import RuntimeJournal, JournalRetained
from karyo_profiles.journal_dns import JournalDNS, recover_profiles
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.journal_netbird import JournalNetBird, state_receipt, service_receipt
from karyo_profiles.journal_network import JournalNetwork, restore_network
from karyo_profiles.logind import Sessions
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


def needs_login(scope):
    deadline = time.monotonic() + 15
    while True:
        if scope.status().get("daemonStatus") == "NeedsLogin": return
        require(time.monotonic() < deadline, "Provider did not reach its pre-login state")
        time.sleep(.1)


def controller():
    packet = sys.stdin.buffer.readline(1025)
    require(len(packet) <= 1024 and packet.endswith(bytes([10])), "Bounded controller input required")
    request = json.loads(packet)
    require(isinstance(request, dict) and set(request) == {"mode"} and request["mode"] in
            {"clean", "active", "service-stopped", "host-restart"}, "Unknown provider controller mode")
    mode = request["mode"]
    store, accounts, sessions = ConfigStore(), LocalAccounts(), Sessions()
    child = accounts.account("kq-switch-work")
    require(child.uid == 1002 and child.gid == 1002, "Unexpected provider QA account")
    source = next(row for row in sessions.all() if row.uid == 1001 and row.name == "karyoqa" and row.active)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"], "Existing resources require review")
        with store.locked():
            network = JournalNetwork.start(journal, store, child, source)
            physical = discover_underlay(network.network.command)
            forwarding = JournalForwarding.start(journal, network, physical)
            dns = JournalDNS.start(journal, network, discover_dns(physical, network.network.command))
            provider = JournalNetBird.start(journal, dns)
            needs_login(provider.scope)
            provider.verify()
            if mode == "service-stopped":
                provider.update(pending="service-stop")
                provider.scope.close()  # Simulate loss of only the publication ACK.
        print(json.dumps({"ready": mode, "pid": os.getpid(), "record": journal.record(network.token)}), flush=True)
        acknowledgement = sys.stdin.buffer.read(1)
        if mode == "clean" and acknowledgement == b"x":
            with store.locked():
                provider.close(); dns.close(); forwarding.close(); network.close()
            return
        raise RuntimeError("The crash branch requires termination of this captured controller")


def unit_info():
    return dict(line.split("=", 1) for line in run("/usr/bin/systemctl", "show",
        "karyo-profile-host.service", "-p", "InvocationID", "-p", "MainPID", "-p", "ActiveState").splitlines())


def host_recovery(expected):
    with RuntimeJournal() as journal:
        require(journal.snapshot()["profiles"] == [expected], "Unexpected journal before host restart")
    run("/usr/bin/systemctl", "start", "karyo-profile-host.socket")
    node, unit = socket_node(), None
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as trigger:
            trigger.settimeout(2); trigger.connect(SOCKET_ADDRESS)
            deadline = time.monotonic() + 15
            while True:
                current = unit_info()
                if current["ActiveState"] == "active" and int(current["MainPID"]) > 1 and len(current["InvocationID"]) == 32:
                    unit = current; break
                require(time.monotonic() < deadline, "Host service did not activate for recovery")
                time.sleep(.05)
        while True:
            require(unit_info() == unit, "Host recovery service identity changed")
            state = Path("/run/karyo/profile-host/state.json")
            info = state.stat()
            require(info.st_uid == 0 and info.st_nlink == 1 and info.st_mode & 0o777 == 0o600
                    and info.st_size <= 1024 * 1024, "Unsafe recovered journal")
            if not json.loads(state.read_bytes())["profiles"]: break
            require(time.monotonic() < deadline, "Host startup did not reconcile the provider")
            time.sleep(.05)
        try: RuntimeJournal()
        except JournalRetained: pass
        else: raise RuntimeError("Running host lost lifetime journal ownership")
    finally:
        if unit is not None:
            current = unit_info()
            require(current["InvocationID"] == unit["InvocationID"] and current["MainPID"] in {unit["MainPID"], "0"},
                    "Changed host recovery service retained")
            run("/usr/bin/systemctl", "stop", "karyo-profile-host.service")
        require(unit_info()["ActiveState"] == "inactive", "Uncaptured host service retained")
        require(socket_node() == node, "Changed host socket retained")
        run("/usr/bin/systemctl", "stop", "karyo-profile-host.socket")


def reattach_and_close(journal, store, record):
    with store.locked():
        network = restore_network(store, record, require_empty=False)
        require(network is not None, "Provider network vanished")
        dns = provider = None
        try:
            wrapped = JournalNetwork(journal, network)
            dns = JournalDNS.restore_observers(journal, wrapped)
            provider = JournalNetBird.restore(journal, dns)
            provider.verify(); needs_login(provider.scope)
            require(state_receipt(provider.scope.state) == record["provider"]["state"]
                    and service_receipt(provider.scope) == record["provider"]["service"],
                    "The live provider was restarted or replaced during reattachment")
            provider.close()
        finally:
            if provider is not None: provider.release_handles()
            if dns is not None: dns.release_handles()
            if network.fd is not None: os.close(network.fd); network.fd = None
    recover_profiles(journal, store)  # Normal dependency order for remaining layers.


if sys.argv[1:]:
    controller(); raise SystemExit(0)

store, accounts, sessions = ConfigStore(), LocalAccounts(), Sessions()
source = next(row for row in sessions.all() if row.uid == 1001 and row.name == "karyoqa" and row.active)
require(Path("/proc/sys/net/ipv4/ip_forward").read_text().strip() == "0", "QA forwarding baseline changed")
require(not Path("/var/lib/karyo/profile-vpn/1002").exists(), "Prior provider state requires review")
for unit in ("karyo-profile-host.service", "karyo-profile-host.socket", "netbird.service"):
    require(run("/usr/bin/systemctl", "show", unit, "-p", "ActiveState", "--value") == "inactive", "Host transport/global VPN active")
original = snapshot()
original_conf = {str(path): path.read_text() for path in Path("/proc/sys/net/ipv4/conf").glob("*/*")}
original_accounts = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"), Path("/etc/shadow"), Path("/etc/group"), Path("/etc/gshadow"), *Path("/etc/pam.d").glob("common-*")]}
with RuntimeJournal() as journal:
    require(not journal.snapshot()["profiles"], "Previous provider ownership requires review")
run("/usr/local/libexec/karyo-qa-two-sessions", "prepare")
state_identity = None
try:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("1\n")
    for mode in ("clean", "active", "service-stopped", "host-restart"):
        with subprocess.Popen(["/usr/bin/python3", "-I", __file__, "--controller"], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
            process.stdin.write((json.dumps({"mode": mode}) + chr(10)).encode()); process.stdin.flush()
            try:
                require(select.select([process.stdout], [], [], 35)[0], "Provider controller readiness timeout")
                packet = process.stdout.readline(32769)
                if not packet: raise RuntimeError("Provider controller failed: " + process.stderr.read(16384).decode())
                require(len(packet) <= 32768, "Provider receipt exceeds its limit")
                ready = json.loads(packet)
                require(ready["ready"] == mode and ready["pid"] == process.pid, "Unexpected provider controller")
                record = ready["record"]
                current = tuple(record["provider"]["state"]["directory"])
                if state_identity is None: state_identity = current
                else: require(state_identity == current, "Persistent state was recreated between runtime leases")
                try: RuntimeJournal()
                except JournalRetained: pass
                else: raise RuntimeError("A concurrent controller acquired the journal")
                if mode == "clean":
                    process.stdin.write(b"x"); process.stdin.flush()
                    require(process.wait(timeout=20) == 0, "Normal cleanup failed: " + process.stderr.read(16384).decode())
                else:
                    process.kill(); require(process.wait(timeout=5) == -9, "Captured controller did not terminate")
            finally:
                if process.poll() is None: process.kill(); process.wait(timeout=5)
        if mode == "host-restart": host_recovery(record)
        with RuntimeJournal() as journal:
            if mode not in {"clean", "host-restart"}:
                require(journal.snapshot()["profiles"] == [record], "Crash changed the provider receipt")
                if mode == "active": reattach_and_close(journal, store, record)
                else: recover_profiles(journal, store)
            require(not journal.snapshot()["profiles"], "Provider recovery left journal resources")
        require(snapshot() == original and sessions.get(source.id) == source, "Original Home state changed")
        for path in ("/run/karyo-netbird-" + record["token"], "/run/netns/karyo-" + record["token"]):
            require(not os.path.lexists(path), "Captured provider runtime remains")
        print(json.dumps({"mode": mode, "result": "clean", "state_reused": mode != "clean"}), flush=True)
finally:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("0\n")
    for name, value in original_conf.items():
        path = Path(name)
        if path.read_text() != value: path.write_text(value)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"], "Retained provider resources; do not remove QA state/account")
    require(not run("/usr/sbin/ip", "netns", "list"), "Captured namespace remains")
    if state_identity is not None:
        state = Path("/var/lib/karyo/profile-vpn/1002/netbird")
        info = state.stat()
        require((info.st_dev, info.st_ino) == state_identity, "QA provider state was replaced")
        require(json.loads((state / "owner.json").read_text())["account"]["name"] == "kq-switch-work", "QA ownership stamp changed")
        # Explicit removal of this previously absent fixture; never product cleanup.
        shutil.rmtree(state); state.parent.rmdir()
    run("/usr/local/libexec/karyo-qa-two-sessions", "cleanup")
require(snapshot() == original and sessions.get(source.id) == source, "Original network/login changed")
require({path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}
        == original_accounts, "Account/common PAM restoration failed")
require(not run("/usr/bin/dpkg", "-V", "karyo-profile-accounts", "karyo-profile-host", "karyo-profile-runtime"), "Installed code changed")
require(not run("/usr/bin/dpkg", "--audit"), "Package audit failed")
print("PASS actual pre-login provider reattachment, lost stop ACK and host startup recovery", flush=True)
