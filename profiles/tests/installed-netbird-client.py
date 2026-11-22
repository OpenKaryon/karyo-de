#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed-host real NetBird client/GUI and synthetic browser-loopback acceptance."""
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import select
import socket
import subprocess
import sys
import time
import urllib.request

if socket.gethostname() not in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
    raise SystemExit("Use only the fixed disposable QA hosts")
sys.dont_write_bytecode = True


def require(value, message):
    if not value: raise RuntimeError(message)


def run(*args):
    return subprocess.check_output(args, text=True, timeout=25).strip()


if sys.argv[1:] == ["--callback-driver"]:
    require(os.getuid() == os.geteuid() == 1002, "Only the fixed QA child may drive a callback")
    env = dict(os.environ, BROWSER="/usr/local/libexec/karyo-qa-netbird-browser", XDG_CURRENT_DESKTOP="generic")
    before = time.monotonic()
    subprocess.run(["/usr/libexec/karyo/netbird-opener/xdg-open", "http://127.0.0.1:53000/karyo-qa"],
                   env=env, check=True, timeout=3)
    require(time.monotonic() - before < 2, "Browser handoff waited for the cold browser")
    class Handler(http.server.BaseHTTPRequestHandler):
        received = False
        def do_GET(self):
            require(self.path == "/karyo-qa", "Unexpected synthetic callback path")
            Handler.received = True
            self.send_response(200); self.end_headers(); self.wfile.write(b"karyo-qa-ok")
        def log_message(self, *args): pass
    with http.server.HTTPServer(("127.0.0.1", 53000), Handler) as server:
        server.timeout = 10
        server.handle_request()
    require(Handler.received, "The private synthetic callback was not received")
    namespace = os.stat("/proc/self/ns/net")
    print(json.dumps({"async_handoff": True, "private_callback": True,
                      "network": [namespace.st_dev, namespace.st_ino]}), flush=True)
    raise SystemExit(0)
if sys.argv[1:2] == ["--fake-browser"]:
    require(os.getuid() == os.geteuid() == 1002 and sys.argv[2:] == ["http://127.0.0.1:53000/karyo-qa"],
            "Unknown cold-browser fixture")
    time.sleep(.5)
    deadline = time.monotonic() + 8
    while True:
        try:
            with urllib.request.urlopen(sys.argv[2], timeout=1) as response:
                require(response.read() == b"karyo-qa-ok", "Wrong synthetic callback response")
            break
        except OSError:
            require(time.monotonic() < deadline, "The browser could not reach the later callback listener")
            time.sleep(.1)
    raise SystemExit(0)
if sys.argv[1:] in (["--catalog-work"], ["--catalog-home"]):
    import ctypes
    work = sys.argv[1] == "--catalog-work"
    require(os.getuid() == os.geteuid() == (1002 if work else 1001), "Unknown catalog QA account")
    gio = ctypes.CDLL("libgio-2.0.so.0")
    gio.g_desktop_app_info_new.argtypes = [ctypes.c_char_p]; gio.g_desktop_app_info_new.restype = ctypes.c_void_p
    info = gio.g_desktop_app_info_new(b"org.wails.netbird.desktop")
    value = {"visible": bool(info), "uid": os.getuid(), "data_dirs": os.environ.get("XDG_DATA_DIRS", "")}
    if info:
        for function, key in (("g_app_info_get_id", "id"), ("g_app_info_get_name", "name"),
                ("g_app_info_get_executable", "executable"), ("g_desktop_app_info_get_filename", "filename")):
            method = getattr(gio, function); method.argtypes = [ctypes.c_void_p]; method.restype = ctypes.c_char_p
            value[key] = method(info).decode()
        gio.g_app_info_should_show.argtypes = [ctypes.c_void_p]; gio.g_app_info_should_show.restype = ctypes.c_int
        value["should_show"] = bool(gio.g_app_info_should_show(info))
        objects = ctypes.CDLL("libgobject-2.0.so.0"); objects.g_object_unref.argtypes = [ctypes.c_void_p]
        objects.g_object_unref(info)
    print(json.dumps(value), flush=True); raise SystemExit(0)
require(os.getuid() == os.geteuid() == 0 and sys.argv[1:] in ([], ["--window-only"]), "Use the root QA driver")
window_only = sys.argv[1:] == ["--window-only"]
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import discover_dns
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.journal_session import JournalSession
from karyo_profiles.logind import Sessions
from karyo_profiles.journal_netbird import JournalNetBird
from karyo_profiles.returning import ReturnAfterExit
from karyo_profiles.services import program_identity
from karyo_profiles.storage import ConfigStore
from karyo_profiles.switching import ExistingSwitcher
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.user_manager import ManagerPending


def user(*args):
    return ["/usr/sbin/runuser", "-u", "kq-switch-work", "--", "/usr/bin/env", "XDG_RUNTIME_DIR=/run/user/1002",
            "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus", "WAYLAND_DISPLAY=wayland-0", *args]


def wait(predicate, message, seconds=30):
    deadline = time.monotonic() + seconds
    while not predicate():
        require(time.monotonic() < deadline, message)
        time.sleep(.1)


def unit(name):
    return dict(line.split("=", 1) for line in run(*user("/usr/bin/systemctl", "--user", "show", name,
        "-p", "InvocationID", "-p", "MainPID", "-p", "ControlGroup", "-p", "ActiveState")).splitlines())


def sandbox_renderer(gui):
    group = Path("/sys/fs/cgroup" + gui["ControlGroup"])
    host_user = os.stat("/proc/" + gui["MainPID"] + "/ns/user")
    diagnostics = []
    parent = Path("/proc", gui["MainPID"])
    env = (parent / "environ").read_bytes().split(bytes([0]))
    diagnostics.append({"gui_flags": {name: any(row.startswith(name.encode() + b"=") for row in env)
        for name in ("SNAP", "SNAP_NAME", "SNAP_REVISION", "WEBKIT_FORCE_SANDBOX", "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS")},
        "flatpak_marker": (parent / "root/.flatpak-info").exists(),
        "container_marker": (parent / "root/run/.containerenv").exists()})
    for value in (group / "cgroup.procs").read_text().splitlines():
        if not value.isdecimal(): continue
        process = Path("/proc", value)
        try:
            argv = (process / "cmdline").read_bytes().split(bytes([0]))
            if not argv: continue
            status = dict(line.split(":", 1) for line in (process / "status").read_text().splitlines() if ":" in line)
            user = (process / "ns/user").stat()
            diagnostics.append({"program": Path(os.fsdecode(argv[0])).name, "comm": (process / "comm").read_text().strip(),
                "nnp": status["NoNewPrivs"].strip(), "seccomp": status["Seccomp"].strip(),
                "user_ns": [user.st_dev, user.st_ino], "gui_user_ns": [host_user.st_dev, host_user.st_ino]})
            if ((Path(os.fsdecode(argv[0])).name == "WebKitWebProcess" or (process / "comm").read_text().strip() == "WebKitWebProces")
                    and status["NoNewPrivs"].strip() == "1" and status["Seccomp"].strip() == "2"
                    and (user.st_dev, user.st_ino) != (host_user.st_dev, host_user.st_ino)):
                return True
        except (FileNotFoundError, ProcessLookupError): pass
    Path("/var/log/karyo-qa/netbird-sandbox-diagnostic.json").write_text(json.dumps(diagnostics))
    return False


store, accounts, sessions = ConfigStore(), LocalAccounts(), Sessions()
source = next(row for row in sessions.all() if row.uid == 1001 and row.name == "karyoqa" and row.active)
require(Path("/proc/sys/net/ipv4/ip_forward").read_text().strip() == "0", "Changed QA forwarding baseline")
require(not Path("/var/lib/karyo/profile-vpn/1002").exists(), "Prior provider state requires review")
ready = Path("/var/log/karyo-qa/netbird-client-ready.json")
ack = Path("/var/log/karyo-qa/netbird-client-frame-ack")
require(not ready.exists() and not ack.exists(), "Prior visual QA handshake requires review")
original_conf = {str(p): p.read_text() for p in Path("/proc/sys/net/ipv4/conf").glob("*/*")}
original_accounts = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [Path("/etc/passwd"), Path("/etc/shadow"), Path("/etc/group"), Path("/etc/gshadow"), *Path("/etc/pam.d").glob("common-*")]}
with RuntimeJournal() as journal: require(not journal.snapshot()["profiles"], "Prior journal resources")
run("/usr/local/libexec/karyo-qa-two-sessions", "prepare")
state_identity = None
try:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("1\n")
    with RuntimeJournal() as journal:
        with store.locked():
            network = JournalNetwork.start(journal, store, accounts.account("kq-switch-work"), source)
            physical = discover_underlay(network.network.command)
            forwarding = JournalForwarding.start(journal, network, physical)
            dns = JournalDNS.start(journal, network, discover_dns(physical, network.network.command))
            captured = JournalNetBird.start(journal, dns)
            state_identity = captured.scope.state.identity
            graph = JournalSession.start(journal, dns)
            captured.release_handles()
            graph.enter(accounts.account("karyoqa"))
        wait(lambda: Path("/run/user/1002/wayland-0").exists(), "Work compositor did not become ready")
        provider = graph.provider.scope
        name = "karyo-qa-netbird-gui-" + network.token + ".service"
        gui = None
        try:
            env = "KARYO_NETBIRD_ENDPOINT=" + str(provider.endpoint)
            # Direct ordinary client from the host network must fail even for
            # the right UID. The scoped manager is the positive launch path.
            rejected = subprocess.run(user("/usr/bin/env", env, "/usr/bin/karyo-netbird-cli", "status", "--json"),
                                      capture_output=True, text=True, timeout=10)
            require(rejected.returncode != 0, "Client accepted the wrong network/resolver context")
            result = subprocess.run(user("/usr/bin/systemd-run", "--user", "--wait", "--pipe", "--collect",
                "--unit=karyo-qa-netbird-status.service", "--", "/usr/bin/karyo-netbird-cli", "status", "--json"),
                capture_output=True, text=True, timeout=20)
            require(result.returncode == 0 and isinstance(json.loads(result.stdout), dict), "Scoped CLI failed: " + result.stderr)
            catalog = json.loads(run(*user("/usr/bin/systemd-run", "--user", "--wait", "--pipe", "--collect",
                "--unit=karyo-qa-netbird-catalog.service", "--", "/usr/local/libexec/karyo-qa-netbird-client", "--catalog-work")))
            require(catalog.get("visible") and catalog.get("should_show") and catalog.get("id") == "org.wails.netbird.desktop"
                and catalog.get("executable") == "/usr/bin/karyo-netbird-gui"
                and catalog.get("filename") == "/usr/share/karyo/profile-catalog/netbird/applications/org.wails.netbird.desktop",
                "Work did not discover the packaged scoped launcher")
            home_catalog = json.loads(run("/usr/sbin/runuser", "-u", "karyoqa", "--", "/usr/bin/env",
                "XDG_RUNTIME_DIR=/run/user/1001", "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus",
                "/usr/bin/systemd-run", "--user", "--wait", "--pipe", "--collect", "--unit=karyo-qa-netbird-home-catalog.service",
                "--", "/usr/local/libexec/karyo-qa-netbird-client", "--catalog-home"))
            require(not home_catalog["visible"] and "/usr/share/karyo/profile-catalog/netbird" not in home_catalog["data_dirs"],
                    "Home gained the private provider catalog")
            require(journal.snapshot()["schema"] == 7 and journal.record(network.token)["session"]["catalog"] == "netbird",
                    "Catalog launch policy is not captured")
            callback = subprocess.run(user("/usr/bin/systemd-run", "--user", "--wait", "--pipe", "--collect",
                "--unit=karyo-qa-netbird-callback.service", "--", "/usr/local/libexec/karyo-qa-netbird-client", "--callback-driver"),
                capture_output=True, text=True, timeout=20)
            require(callback.returncode == 0, "Synthetic browser callback failed: " + callback.stderr)
            callback_result = json.loads(callback.stdout)
            require(callback_result["network"] == list(network.network.identity), "Callback used a different desktop network")
            run(*user("/usr/bin/systemd-run", "--user", "--quiet", "--collect", "--unit=" + name,
                "--property=Type=exec", "--setenv=WAYLAND_DISPLAY=wayland-0",
                "--setenv=GDK_BACKEND=wayland", "--", "/usr/bin/karyo-netbird-gui"))
            def window():
                return "netbird" in run(*user("/usr/bin/wlrctl", "toplevel", "list")).lower()
            wait(window, "Actual NetBird GUI did not map a window", seconds=35)
            gui = unit(name)
            require(gui["ActiveState"] == "active" and int(gui["MainPID"]) > 1, "GUI unit is not live")
            process = Path("/proc", gui["MainPID"])
            executable = (process / "exe").stat()
            require((executable.st_dev, executable.st_ino) == program_identity("/usr/bin/netbird-ui"), "GUI executable changed")
            status = dict(line.split(":", 1) for line in (process / "status").read_text().splitlines() if ":" in line)
            require(status["Uid"].split() == ["1002"] * 4 and int(status["CapInh"], 16) == int(status["CapAmb"], 16) == 0,
                    "GUI has unexpected credentials or inherited capabilities")
            require([(process / "ns/net").stat().st_dev, (process / "ns/net").stat().st_ino] == list(network.network.identity),
                    "GUI is outside Work network")
            private = Path("/run", dns.files.plan.runtime_name).stat()
            mounted = (process / "root/run/systemd/resolve").stat()
            require((private.st_dev, private.st_ino) == (mounted.st_dev, mounted.st_ino), "GUI resolver is not private")
            environment = (process / "environ").read_bytes().split(bytes([0]))
            require(b"WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=0" in environment
                    and not any(row.split(b"=", 1)[0] in {b"WEBKIT_DISABLE_SANDBOX", b"G_DISABLE_SANDBOX"} for row in environment),
                    "GUI did not preserve the renderer sandbox")
            expected_data = b"XDG_DATA_DIRS=/usr/share/karyo/profile-catalog/netbird:/usr/local/share:/usr/share"
            observed = [{"role": role, "pid": pid, "data": [row.decode() for row in
                Path("/proc", str(pid), "environ").read_bytes().split(bytes([0])) if row.startswith(b"XDG_DATA_DIRS=")]}
                for role, pid in (("manager", graph.manager.identity.pid), ("graph", graph.graph.pid), ("gui", int(gui["MainPID"])))]
            print(json.dumps({"catalog_environments": observed}), flush=True)
            for observation in observed:
                allowed = [[expected_data.decode()]]
                if observation["role"] == "gui" and socket.gethostname() == "karyo-de-ubuntu-acceptance":
                    allowed.append([expected_data.decode() + ":/var/lib/snapd/desktop"])
                require(observation["data"] in allowed,
                        "Captured owner lost its private catalog prefix or gained an unknown data root")
            require(b"WEBKIT_DISABLE_DMABUF_RENDERER=1" in environment, "GUI lost the upstream rendering launch default")
            print(json.dumps({"catalog_work": catalog, "catalog_home": home_catalog,
                "native_toplevels": run(*user("/usr/bin/wlrctl", "toplevel", "list")).splitlines()}), flush=True)
            if not window_only:
                wait(lambda: sandbox_renderer(gui), "WebKit renderer sandbox was not observed")
            sandboxed = sandbox_renderer(gui)
            with store.locked(): graph.verify_consumers()
            ready.write_text(json.dumps({"token": network.token, "pid": int(gui["MainPID"])}))
            print(json.dumps({"qa_gui_ready": network.token, "pid": int(gui["MainPID"])}), flush=True)
            require(select.select([sys.stdin], [], [], 35)[0] and sys.stdin.readline(128).strip() == network.token,
                    "Visual QA frame was not acknowledged")
            print(json.dumps({"ordinary_cli": True, "wrong_context_denied": True, "actual_gui_window": True,
                              "zero_ambient_inheritable_caps": True, "sandboxed_webkit_renderer": sandboxed, "window_only_gate": window_only, "callback": callback_result}), flush=True)
        finally:
            if gui is not None:
                require(unit(name) == gui, "Captured GUI service changed before stop")
                run(*user("/usr/bin/systemctl", "--user", "stop", name))
            ExistingSwitcher(store).switch(accounts.account("kq-switch-work"), "karyoqa")
            deadline = time.monotonic() + 30
            while True:
                try:
                    with store.locked(): graph.close()
                    break
                except ManagerPending:
                    require(time.monotonic() < deadline, "User manager did not naturally close")
                    time.sleep(.1)
            with store.locked():
                graph.provider.close(); dns.close(); forwarding.close(); network.close()
            graph.release_handles()
            require(not journal.snapshot()["profiles"], "Client test left runtime receipts")
finally:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("0\n")
    for name, value in original_conf.items():
        if Path(name).read_text() != value: Path(name).write_text(value)
    with RuntimeJournal() as journal: require(not journal.snapshot()["profiles"], "Retained resources; keep QA state/account")
    if state_identity is not None:
        path = Path("/var/lib/karyo/profile-vpn/1002/netbird")
        info = path.stat(); require((info.st_dev, info.st_ino) == state_identity, "QA provider state changed")
        shutil.rmtree(path); path.parent.rmdir()
    for path in (ready, ack):
        if path.exists(): path.unlink()
    run("/usr/local/libexec/karyo-qa-two-sessions", "cleanup")
require(sessions.get(source.id) == source, "Original Home login changed")
require({name: hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in original_accounts} == original_accounts,
        "Account/common PAM restoration failed")
require(not run("/usr/bin/dpkg", "-V", "karyo-profile-accounts", "karyo-profile-netbird-client", "karyo-profile-host", "karyo-profile-runtime"),
        "Installed client payload changed")
print("PASS actual NetBird GUI/client and nonblocking synthetic private callback", flush=True)
