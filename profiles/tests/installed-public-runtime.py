#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cold public-helper startup on the two fixed disposable Karyo QA hosts.

Root-only context fixtures test actual installed authorization and host IPC;
this is not a substitute for an interactive polkit/password acceptance test.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time

if (os.getuid() != 0 or os.geteuid() != 0 or socket.gethostname() not in
        {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}):
    raise SystemExit("Use only the fixed disposable hosts as root")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.consoles import KernelConsoles
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_session import stat_start
from karyo_profiles.logind import Sessions
from karyo_profiles.model import DesktopSettings
from karyo_profiles.networks import NetworkSpec
from karyo_profiles.runtime_policy import enabled, forwarding_ready
from karyo_profiles.storage import ConfigStore


def require(value, message):
    if not value: raise RuntimeError(message)


def run(*args):
    return subprocess.check_output(args, text=True, timeout=20).strip()


def state():
    path = Path("/run/karyo/profile-host/state.json")
    info = path.stat()
    require(info.st_uid == 0 and info.st_nlink == 1 and info.st_mode & 0o777 == 0o600,
            "Unsafe public runtime journal")
    return json.loads(path.read_bytes())["profiles"]


def processes(uid, program):
    result = []
    for path in Path("/proc").iterdir():
        if not path.name.isdecimal(): continue
        try:
            row = next(line for line in (path / "status").read_text().splitlines() if line.startswith("Uid:"))
            argv = (path / "cmdline").read_bytes().split(bytes([0]))
            if str(uid) in row.split()[1:] and argv and Path(os.fsdecode(argv[0])).name == program:
                result.append(int(path.name))
        except (FileNotFoundError, ProcessLookupError): pass
    return sorted(result)


def context(name):
    expected = {"karyoqa": 1001, "kq-switch-work": 1002}[name]
    account = LocalAccounts().account(name)
    require(account.uid == expected, "Unexpected fixed QA context account")
    rows = [row for row in Sessions().all() if row.name == name and row.uid == expected and row.active]
    require(len(rows) == 1, "QA source is not uniquely active")
    source = rows[0]
    scope = "session-" + source.id + ".scope"
    path = Path("/sys/fs/cgroup/user.slice/user-" + str(expected) + ".slice") / scope
    require(run("/usr/bin/systemctl", "show", scope, "-p", "ControlGroup", "--value") == str(path)[14:],
            "Source cgroup identity changed")
    pid = None
    if expected == 1002:
        records = state()
        require(len(records) == 1 and records[0]["account"]["uid"] == expected, "Ambiguous Work receipt")
        graph = records[0]["session"]["graphical"]
        require(graph["login"]["id"] == source.id and stat_start(graph["pid"]) == graph["start"],
                "Captured Work source changed")
        pid = graph["pid"]
    path.joinpath("cgroup.procs").write_text(str(os.getpid()))
    if pid is not None:
        libc = ctypes.CDLL(None, use_errno=True)
        for kind, flag in (("net", 0x40000000), ("mnt", 0x00020000)):
            fd = os.open("/proc/" + str(pid) + "/ns/" + kind, os.O_RDONLY | os.O_CLOEXEC)
            try: require(libc.setns(fd, flag) == 0, "Could not enter captured Work context")
            finally: os.close(fd)
    return expected


if sys.argv[1:2] == ["--helper"]:
    require(sys.argv[2:] in (["karyoqa", "kq-switch-work"], ["kq-switch-work", "karyoqa"]), "Unknown helper fixture")
    uid = context(sys.argv[2])
    helper = "/usr/libexec/karyo/desktop-switch"
    os.execve(helper, [helper, sys.argv[3]], {"PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
              "LANG": "C.UTF-8", "PKEXEC_UID": str(uid)})
if sys.argv[1:] == ["--probe"]:
    context("kq-switch-work")
    signal.alarm(15)
    answers = socket.getaddrinfo("example.org", 443, socket.AF_INET, socket.SOCK_STREAM)
    require(bool(answers), "Private mounted DNS returned no IPv4 answers")
    with socket.create_connection(("1.1.1.1", 443), timeout=5) as connection:
        address = connection.getsockname()[0]
    require(address == NetworkSpec(1002, "0" * 32).guest, "Work TCP did not use its private source")
    print(json.dumps({"private_dns_answers": len(answers), "tcp_source": address}), flush=True)
    raise SystemExit(0)
require(not sys.argv[1:], "Unknown public-runtime QA arguments")


def switch(source, target):
    result = subprocess.run(["/usr/bin/python3", "-I", __file__, "--helper", source, target],
                            text=True, capture_output=True, timeout=65)
    require(result.returncode == 0, "Installed public desktop-switch failed: " + result.stderr)
    reply = json.loads(result.stdout)
    require(reply["user"] == target and reply["changed"], "Public helper returned an unexpected target")
    return reply


def wait_for(predicate, message, seconds=35):
    deadline = time.monotonic() + seconds
    while not predicate():
        require(time.monotonic() < deadline, message)
        time.sleep(.1)


def crash(record):
    graph = record["session"]["graphical"]
    fd = os.pidfd_open(graph["pid"])
    try:
        executable = os.stat("/proc/" + str(graph["pid"]) + "/exe")
        require([executable.st_dev, executable.st_ino] == graph["compositor"]
                and stat_start(graph["pid"]) == graph["start"], "Captured compositor identity changed")
        require(run("/usr/bin/systemctl", "show", graph["login"]["scope"], "-p", "InvocationID", "--value")
                == graph["scope_invocation"], "Captured compositor scope changed")
        signal.pidfd_send_signal(fd, signal.SIGKILL)
    finally: os.close(fd)


require(enabled(), "Explicit administrator runtime activation is required first")
forwarding_ready()
store, accounts, sessions = ConfigStore(), LocalAccounts(), Sessions()
source = next(row for row in sessions.all() if row.uid == 1001 and row.name == "karyoqa" and row.active)
main_shell = processes(1001, "karyo-shell")
require(len(main_shell) == 1, "The original Home shell is ambiguous")
original_accounts = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"), Path("/etc/shadow"), Path("/etc/group"), Path("/etc/gshadow"),
     *Path("/etc/pam.d").glob("common-*")]}
require(run("/usr/bin/systemctl", "is-active", "karyo-profile-host.socket") == "active", "Host socket inactive")
require(run("/usr/bin/systemctl", "show", "karyo-profile-host.service", "-p", "ActiveState", "--value")
        == "inactive", "A prior controller requires review")
with RuntimeJournal() as journal:
    require(not journal.snapshot()["profiles"], "Retained resources prevent public QA")
run("/usr/local/libexec/karyo-qa-two-sessions", "prepare")
child = accounts.account("kq-switch-work")
original_policy = store.settings(child.name)
try:
    for mode in ("logout", "crash", "locked-crash"):
        policy = DesktopSettings(password="home" if mode == "locked-crash" else "none")
        with store.locked():
            require(store.settings(child.name) == original_policy, "QA protection changed")
            store.save_settings(child.name, policy)
        cold = switch("karyoqa", child.name)
        require(cold.get("starting") is True, "Public helper did not report a fresh session")
        require(len(state()) == 1, "Public startup did not create exactly one receipt")
        record = state()[0]
        require(record["account"]["uid"] == 1002 and "forwarding" in record and "session" in record,
                "Public startup omitted owned graph layers")
        identifier = record["session"]["graphical"]["login"]["id"]
        if mode == "locked-crash":
            wait_for(lambda: sessions.get(identifier).locked and len(processes(1002, "karyo-lock")) == 1,
                     "Protected cold startup never locked")
            require(not processes(1002, "karyo-shell") and not processes(1002, "karyo-editor"),
                    "Protected startup exposed applications before authentication")
            packets = {"initial_lock_without_shell": True}
        else:
            wait_for(lambda: len(processes(1002, "karyo-shell")) == 1, "Public Work shell never became ready")
            result = subprocess.run(["/usr/bin/python3", "-I", __file__, "--probe"],
                                    text=True, capture_output=True, timeout=25)
            require(result.returncode == 0, "Work private network probe failed: " + result.stderr)
            packets = json.loads(result.stdout)
            if mode == "logout":
                require(not switch(child.name, "karyoqa").get("starting", False), "Home was recreated")
                require(not switch("karyoqa", child.name).get("starting", False), "Running Work was recreated")
                require(state()[0]["token"] == record["token"], "Existing switch replaced ownership token")
        if mode == "logout":
            run("/usr/sbin/runuser", "-u", child.name, "--", "/usr/bin/env", "XDG_RUNTIME_DIR=/run/user/1002",
                "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus", "/usr/bin/systemd-run", "--user",
                "--wait", "--pipe", "--collect", "--unit=karyo-qa-public-logout.service", "--", "/usr/bin/labwc", "--exit")
        else: crash(record)
        wait_for(lambda: not state(), "Public supervisor did not finish logout cleanup", seconds=40)
        require(sessions.get(source.id) == source and processes(1001, "karyo-shell") == main_shell,
                "The original Home login changed")
        with KernelConsoles() as kernel:
            require(kernel.state().active == source.vt, "Kernel did not return to Home")
        with store.locked():
            require(store.settings(child.name) == policy, "QA policy changed before restoration")
            store.save_settings(child.name, original_policy)
        print(json.dumps({"mode": mode, "cold_public_helper": True, "home_return": True,
                          "cleanup": "complete", "network": packets}), flush=True)
finally:
    require(not state(), "Retained public resources require review; QA account kept")
    require(store.settings(child.name) == original_policy, "QA policy was not restored")
    run("/usr/local/libexec/karyo-qa-two-sessions", "cleanup")
require({path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}
        == original_accounts, "Accounts/common PAM changed")
require(not run("/usr/sbin/ip", "netns", "list"), "Private namespaces remain")
require(not run("/usr/bin/dpkg", "-V", "karyo-profile-accounts", "karyo-profile-host", "karyo-profile-runtime"),
        "Installed public runtime payload changed")
require(not run("/usr/bin/dpkg", "--audit"), "Package audit failed")
print("PASS public cold child startup, private DNS/TCP, existing switches, protected entry and supervised return", flush=True)
