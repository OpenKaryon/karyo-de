# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded root-only transport to the host; never accept forwarded caller IDs."""
import json
import os
import select
from pathlib import Path
import socket
import stat
import struct
import sys
import time

from .credentials import LocalAccounts
from .logind import SessionError, Sessions, listing
from .model import account_name
from .peer import DesktopPeer
from .services import host_controller
from .storage import ConfigStore, trusted_directory
from .switching import DesktopStopped, ExistingSwitcher, ProviderUnavailable

ADDRESS = "/run/karyo/profiles.sock"
REQUEST_LIMIT = 4096
RESPONSE_LIMIT = 64 * 1024


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate desktop protocol field")
        result[key] = value
    return result


def receive(connection, limit, seconds):
    deadline = time.monotonic() + seconds
    data = bytearray()
    expected = None
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Desktop protocol deadline expired")
        connection.settimeout(remaining)
        block = connection.recv(min(8192, limit + 5 - len(data)))
        if not block:
            break
        data.extend(block)
        if len(data) >= 4 and expected is None:
            expected = struct.unpack("!I", data[:4])[0]
            if not 1 <= expected <= limit:
                raise ValueError("Invalid desktop protocol frame size")
        if expected is not None and len(data) > expected + 4:
            raise ValueError("Desktop protocol contains trailing bytes")
    if expected is None or len(data) != expected + 4:
        raise ValueError("Incomplete desktop protocol frame")
    return json.loads(data[4:].decode("utf-8"), object_pairs_hook=unique_object)


def send(connection, value, limit, seconds):
    data = json.dumps(value, ensure_ascii=True, allow_nan=False, separators=(",", ":")).encode("ascii")
    if not 1 <= len(data) <= limit:
        raise ValueError("Desktop protocol response exceeds its limit")
    connection.settimeout(seconds)
    connection.sendall(struct.pack("!I", len(data)) + data)
    connection.shutdown(socket.SHUT_WR)


def operation(packet):
    if not isinstance(packet, dict) or type(packet.get("version")) is not int or packet["version"] != 1:
        raise ValueError("Unsupported desktop protocol version")
    if packet.get("operation") == "list" and set(packet) == {"version", "operation"}:
        return "list", None
    if packet.get("operation") == "switch" and set(packet) == {"version", "operation", "name"}:
        return "switch", account_name(packet["name"])
    raise ValueError("Unsupported desktop operation or fields")


class HostDispatcher:
    def __init__(self, store=None, *, accounts=None, sessions=None, peer=DesktopPeer,
                 switcher=ExistingSwitcher, guard=host_controller):
        self.store = ConfigStore() if store is None else store
        self.accounts = LocalAccounts() if accounts is None else accounts
        self.sessions = Sessions() if sessions is None else sessions
        self.peer, self.switcher, self.guard = peer, switcher, guard

    def handle(self, connection):
        caller = None
        try:
            self.guard()
            # Credentials are captured before parsing input. No UID, owner,
            # session ID, path, namespace or program can come from the packet.
            caller = self.peer(connection, accounts=self.accounts, sessions=self.sessions)
            name, target = operation(receive(connection, REQUEST_LIMIT, 2))
            self.guard()
            if name == "list":
                with self.store.locked():
                    result = listing(caller.verify(), self.store, self.accounts, self.sessions)
            else:
                result = self.switcher(self.store, accounts=self.accounts,
                                       sessions=self.sessions).switch(caller.verify(), target,
                                                                      reauthorize=caller.verify)
            response = {"version": 1, "result": result}
        except PermissionError:
            response = {"version": 1, "error": "denied"}
        except DesktopStopped:
            response = {"version": 1, "error": "stopped"}
        except ProviderUnavailable:
            response = {"version": 1, "error": "provider-unavailable"}
        except (ValueError, UnicodeError, RecursionError):
            response = {"version": 1, "error": "invalid"}
        except (OSError, RuntimeError, TimeoutError, SessionError):
            # Native exceptions may contain privileged paths or account data.
            response = {"version": 1, "error": "failed"}
        finally:
            if caller is not None:
                caller.close()
        try:
            send(connection, response, RESPONSE_LIMIT, 2)
        except (OSError, ValueError, TimeoutError):
            pass  # The peer can close at any time; never repeat an operation.


def socket_node(address=ADDRESS):
    directory = trusted_directory(Path(address).parent)
    try:
        info = os.stat(Path(address).name, dir_fd=directory, follow_symlinks=False)
        if (not stat.S_ISSOCK(info.st_mode) or info.st_uid != 0 or info.st_gid != 0
                or stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1):
            raise PermissionError("Unsafe desktop controller socket")
        return info.st_dev, info.st_ino
    finally:
        os.close(directory)


def activated_listener(environment):
    host_controller()
    if (environment.get("LISTEN_PID") != str(os.getpid()) or environment.get("LISTEN_FDS") != "1"
            or environment.get("LISTEN_FDNAMES") != "profiles"):
        raise PermissionError("The host controller needs its one systemd socket")
    os.set_inheritable(3, False)
    listener = socket.socket(fileno=os.dup(3))
    try:
        if (listener.getsockopt(socket.SOL_SOCKET, socket.SO_DOMAIN) != socket.AF_UNIX
                or listener.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) != socket.SOCK_STREAM
                or listener.getsockopt(socket.SOL_SOCKET, socket.SO_ACCEPTCONN) != 1
                or listener.getsockname() != ADDRESS):
            raise PermissionError("Unexpected desktop controller listener")
        socket_node()
        os.close(3)
        for key in ("LISTEN_PID", "LISTEN_FDS", "LISTEN_FDNAMES"):
            environment.pop(key, None)
        return listener
    except BaseException:
        listener.close()
        raise


def request(packet):
    # Called only by the privileged fixed helper after its local authorization.
    # The host independently authenticates this process from SO_PEERCRED.
    if os.getuid() != 0 or os.geteuid() != 0:
        raise PermissionError("Contact the host through the privileged desktop helper")
    operation(packet)
    before = socket_node()
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect(ADDRESS)
        pid, uid, gid = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        # Socket activation keeps the listener creator credentials: PID 1 is
        # expected, even though the service handles the accepted connection.
        if pid < 1 or uid != 0 or gid != 0 or socket_node() != before:
            raise PermissionError("The desktop host socket identity changed")
        send(connection, packet, REQUEST_LIMIT, 2)
        result = receive(connection, RESPONSE_LIMIT, 40)
    if (not isinstance(result, dict) or type(result.get("version")) is not int or result["version"] != 1
            or set(result) not in ({"version", "result"}, {"version", "error"})):
        raise ValueError("Invalid desktop controller response")
    if "error" in result:
        errors = {"denied": PermissionError, "stopped": DesktopStopped,
                  "invalid": ValueError, "failed": RuntimeError, "provider-unavailable": ProviderUnavailable}
        error = errors.get(result["error"]) if isinstance(result["error"], str) else None
        if error is None:
            raise ValueError("Invalid desktop controller error")
        if error is ProviderUnavailable:
            raise error()
        raise error("The host desktop operation did not finish: " + result["error"])
    return result["result"]



def serve(listener,dispatcher,registry,*,ready=select.select):
    while True:
        registry.poll()
        readable,unused,unused=ready([listener],[],[],.5)
        if not readable:continue
        connection,unused=listener.accept()
        with connection:dispatcher.handle(connection)


def main():
    if sys.argv[1:]:
        print("karyo-profile-host: no arguments are accepted", file=sys.stderr)
        return 1
    try:
        from .journal import RuntimeJournal
        from .journal_session import SessionRegistry
        from .runtime_switching import RuntimeSwitcher
        with activated_listener(os.environ) as listener, RuntimeJournal() as journal:
            dispatcher = HostDispatcher()
            registry=SessionRegistry(journal,dispatcher.store).restore()
            from .network_watch import DNSRefreshMonitor
            registry.network_watch=DNSRefreshMonitor()
            dispatcher.switcher = lambda store, **options: RuntimeSwitcher(store, registry=registry, **options)
            try:
                serve(listener,dispatcher,registry)
            finally:
                registry.release_handles()
    except (OSError, RuntimeError, ValueError, PermissionError):
        print("karyo-profile-host: administrator review is required", file=sys.stderr)
        return 1
