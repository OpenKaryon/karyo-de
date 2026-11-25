# SPDX-License-Identifier: GPL-3.0-or-later
"""Internal PAM handoff on the root host socket; not a VPN provider API."""
import array
import json
import os
from pathlib import Path
import socket
import stat
import struct

from .dns import discover_dns, supported_resolver_layout
from .journal import JournalRetained
from .journal_dns import JournalDNS
from .journal_forwarding import JournalForwarding
from .journal_main import MainEntry
from .journal_network import JournalNetwork
from .journal_network_service import JournalNetworkService
from .host_network_policy import HostNetworkPolicy,policy_loader_ready
from .main_peer import MainPeer, object_identity
from .model import account_name, root_file
from .networks import NetworkCommand
from .runtime_policy import enabled, forwarding_ready
from .services import program_identity
from .storage import trusted_directory
from .underlay import discover_underlay

POLICY = Path("/etc/karyo/main_login.conf")
SERVICES = ("lightdm", "lightdm-autologin")
MAGIC = 0x4b4e4554


def managed_users(path=POLICY):
    directory = None
    try:
        directory = trusted_directory(path.parent)
        text = root_file(path.name, dir_fd=directory)
        info = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
        if stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1:
            raise PermissionError("Main-login policy must be a private root-owned file")
        from .dispatcher import unique_object
        value = json.loads(text, object_pairs_hook=unique_object)
        if (not isinstance(value, dict) or set(value) != {"version", "users"}
                or type(value["version"]) is not int or value["version"] != 1
                or not isinstance(value["users"], list) or not 1 <= len(value["users"]) <= 16):
            raise ValueError("Invalid main-login policy")
        users = tuple(account_name(name) for name in value["users"])
        if len(set(users)) != len(users):raise ValueError("Duplicate managed main account")
        return users
    except FileNotFoundError:
        return ()
    finally:
        if directory is not None:os.close(directory)


def prepare_entry(registry, account, peer, accounts, sessions):
    if not enabled():
        raise PermissionError("Main login requires explicitly enabled private desktop networking")
    forwarding_ready()
    supported_resolver_layout()
    program_identity('/usr/sbin/NetworkManager');policy_loader_ready()
    command = NetworkCommand()
    with command.bounded(8):
        physical = discover_underlay(command)
        seed = discover_dns(physical, command)
    network = dns = service = policy = None
    try:
        network = JournalNetwork.prepare_main(registry.journal, registry.store, account,
                                             accounts=accounts, sessions=sessions, command=command)
        JournalForwarding.start(registry.journal, network, physical)
        dns = JournalDNS.start(registry.journal, network, seed, accounts=accounts,service_view='network-manager')
        service=JournalNetworkService.start(registry.journal,dns)
        policy=HostNetworkPolicy.start(registry.journal,dns)
        return MainEntry.prepare(registry.journal, dns, peer)
    except BaseException:
        if dns is not None:dns.release_handles()
        if network is not None and network.network.fd is not None:
            os.close(network.network.fd);network.network.fd = None
        raise
    finally:
        if policy is not None:policy.release_handles()
        if service is not None:service.release_handles()


class MainDispatcher:
    def __init__(self, registry, accounts, sessions, *, users=managed_users, prepare=prepare_entry,
                 program="/usr/sbin/lightdm", cgroup="/system.slice/lightdm.service", services=SERVICES):
        self.registry, self.accounts, self.sessions = registry, accounts, sessions
        self.users, self.prepare = users, prepare
        # Only trusted local construction can substitute the native QA harness.
        self.program, self.cgroup, self.services = program, cgroup, services

    def claim(self, connection):
        pid, uid, gid = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if uid != 0 or gid != 0:return None
        for scope in self.registry.entries.values():
            if scope.peer is not None and scope.peer.origin["pid"] == pid:
                scope.peer.matches(connection)
                return scope.peer, scope
        try:
            if object_identity("/proc/" + str(pid) + "/exe") != program_identity(self.program):return None
        except FileNotFoundError:return None
        # Capture root process lifetime before parsing even the PAM service.
        return MainPeer.capture(connection, self.services[0], program=self.program, cgroup=self.cgroup), None

    def handle_if_claimed(self, connection):
        claimed = self.claim(connection)
        if claimed is None:return False
        peer, scope = claimed
        descriptors = ()
        status = 1
        try:
            from .dispatcher import receive
            packet = receive(connection, 4096, 2)
            if (not isinstance(packet, dict) or set(packet) != {"version", "operation", "user", "service"}
                    or type(packet["version"]) is not int or packet["version"] != 1
                    or packet["service"] not in self.services):
                raise ValueError("Invalid internal PAM request")
            name = account_name(packet["user"])
            operation = packet["operation"]
            with self.registry.store.locked():
                if scope is None:
                    if operation != "login-prepare":raise JournalRetained("The main PAM entry was not prepared")
                    if name not in self.users():
                        status = 2  # Explicitly unmanaged; PAM returns IGNORE.
                    else:
                        if self.registry.store.ownership().is_child(name):
                            raise PermissionError("A child desktop cannot use a main PAM entry")
                        account = self.accounts.account(name)
                        peer.origin["service"] = packet["service"]
                        scope = self.prepare(self.registry, account, peer, self.accounts, self.sessions)
                        self.registry.entries[scope.token] = scope
                        descriptors = scope.descriptors()
                        status = 0
                else:
                    if name != scope.manager.account.name or packet["service"] != peer.origin["service"]:
                        raise PermissionError("The main PAM request belongs to another entry")
                    peer.matches(connection)
                    if operation == "login-attach":scope.attach()
                    elif operation == "login-close":scope.close_hint()
                    else:raise ValueError("Unsupported main PAM transition")
                    status = 0
        except (OSError, RuntimeError, ValueError, TypeError):
            status = 1
        finally:
            try:
                rights = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", descriptors))] if descriptors else []
                connection.settimeout(2)
                if connection.sendmsg([struct.pack("!III", MAGIC, status, len(descriptors))], rights) != 12:
                    raise OSError("Incomplete internal PAM reply")
            except OSError:pass  # Never retry a request after a broken reply.
            finally:
                for descriptor in descriptors:os.close(descriptor)
                if scope is None:peer.close()
        return True
