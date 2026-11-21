# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned pre-login NetBird service in a profile's network and resolver context.

This primitive does not enable the system NetBird service, import credentials,
or adopt an existing provider. Public VPN activation additionally needs tunnel
inventory and durable provider ownership in the host journal.
"""
from dataclasses import asdict
import json
import os
from pathlib import Path
import socket
import stat
import struct
import time

from .model import root_file
from .netbird_client import endpoint_record
from .services import OwnedUnit, ServiceError, ServiceRetained, host_controller
from .storage import atomic_write, trusted_directory

STATE_ROOT = Path("/var/lib/karyo/profile-vpn")


class NetBirdState:
    def __init__(self, files, root=STATE_ROOT):
        self.files = files
        self.account = files.accounts.by_uid(files.network.spec.uid)
        self.path = root / str(self.account.uid) / "netbird"
        self.owner = None
        self.home_identity = None
        self.fd = None
        self.identity = None

    def capture_home(self):
        path = Path(self.account.home)
        parent = trusted_directory(path.parent)
        try:
            fd = os.open(path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=parent)
        finally:
            os.close(parent)
        try:
            info = os.fstat(fd)
            if info.st_uid != self.account.uid or info.st_mode & 0o002:
                raise ServiceError("The provider account home is not a private account-owned directory")
            return info.st_dev, info.st_ino
        finally:
            os.close(fd)

    def guard(self):
        host_controller()
        self.files.verify()
        if self.files.accounts.by_uid(self.account.uid) != self.account:
            raise ServiceError("The provider state account changed")
        if self.home_identity is not None and self.capture_home() != self.home_identity:
            raise ServiceError("The provider account home generation changed")

    def owner_contents(self, home):
        return json.dumps({"version": 1, "account": asdict(self.account), "home": list(home)},
                          sort_keys=True, separators=(",", ":")) + "\n"

    def prepare(self):
        self.guard()
        if self.fd is not None:
            raise ServiceError("Provider state was already captured")
        self.home_identity = self.capture_home()
        self.owner = self.owner_contents(self.home_identity)
        parent = trusted_directory(self.path.parent, create=True)
        created = False
        try:
            try:
                os.mkdir(self.path.name, 0o700, dir_fd=parent)
                created = True
                os.fsync(parent)
            except FileExistsError:
                pass
            self.fd = os.open(self.path.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC,
                              dir_fd=parent)
        finally:
            os.close(parent)
        try:
            info = os.fstat(self.fd)
            if info.st_uid != 0 or info.st_gid != 0 or stat.S_IMODE(info.st_mode) != 0o700:
                raise ServiceError("Provider state must be a private root-owned directory")
            self.identity = info.st_dev, info.st_ino
            if created:
                if os.listdir(self.fd):
                    raise ServiceError("Fresh provider state unexpectedly contains files")
                atomic_write(self.fd, "owner.json", self.owner.encode("ascii"), mode=0o600)
            self.verify()
            return self
        except BaseException:
            self.release_handles()
            # Unknown state or a lost publication ACK is retained, never adopted
            # by recursively chowning/removing a directory on the next attempt.
            raise

    def verify(self):
        self.guard()
        if self.fd is None or self.identity is None:
            raise ServiceError("The provider state was not captured")
        current = trusted_directory(self.path)
        try:
            info = os.fstat(current)
            if ((info.st_dev, info.st_ino) != self.identity or info.st_gid != 0
                    or stat.S_IMODE(info.st_mode) != 0o700):
                raise ServiceError("The provider state directory changed")
            owner = os.stat("owner.json", dir_fd=current, follow_symlinks=False)
            if owner.st_nlink != 1 or stat.S_IMODE(owner.st_mode) != 0o600:
                raise ServiceError("Unsafe provider ownership stamp")
            if root_file("owner.json", dir_fd=current) != self.owner:
                raise ServiceError("Provider state belongs to another account generation")
        finally:
            os.close(current)

    def release_handles(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None


class NetBirdScope:
    def __init__(self, dns):
        self.dns = dns
        self.files = dns.files
        self.network = self.files.network
        self.state = NetBirdState(self.files)
        self.runtime = "karyo-netbird-" + self.network.spec.token
        self.directory = Path("/run") / self.runtime
        self.socket = self.directory / "control.sock"
        self.unit = OwnedUnit(self.network.store, self.network.spec, "netbird", 0,
                              self.network.command, network=self.network.identity)
        self.socket_identity = None
        self.endpoint_identity = None
        self.endpoint_contents = None

    @property
    def address(self):
        return "unix://" + str(self.socket)

    def properties(self):
        plan = self.files.plan
        return ("User=root", "Group=" + str(plan.gid),
                "NetworkNamespacePath=" + self.network.path,
                "RuntimeDirectory=" + self.runtime, "RuntimeDirectoryMode=0750", "UMask=0077",
                "Environment=DBUS_SYSTEM_BUS_ADDRESS=" + plan.bus_address + " NB_STATE_DIR=" + str(self.state.path),
                "BindReadOnlyPaths=/run/" + plan.runtime_name + ":/run/systemd/resolve",
                "ReadWritePaths=" + str(self.state.path) + " " + str(self.directory),
                "InaccessiblePaths=-/run/dbus/system_bus_socket -/run/NetworkManager -/var/lib/netbird -/etc/netbird",
                "ProtectSystem=strict", "PrivateTmp=yes", "NoNewPrivileges=yes",
                "CapabilityBoundingSet=CAP_NET_ADMIN CAP_NET_RAW CAP_NET_BIND_SERVICE")

    def socket_node(self):
        fd = trusted_directory(self.directory)
        try:
            info = os.fstat(fd)
            if info.st_gid != self.files.plan.gid or stat.S_IMODE(info.st_mode) != 0o750:
                raise ServiceError("Unsafe private provider runtime directory")
            value = os.stat(self.socket.name, dir_fd=fd, follow_symlinks=False)
            if (not stat.S_ISSOCK(value.st_mode) or value.st_uid != 0
                    or value.st_gid != self.files.plan.gid or value.st_nlink != 1):
                raise ServiceError("Unsafe private provider socket")
            return info.st_dev, info.st_ino, value.st_dev, value.st_ino
        finally:
            os.close(fd)

    @property
    def endpoint(self):
        return self.directory / "client.json"

    def endpoint_text(self):
        resolver = os.stat("/run/" + self.files.plan.runtime_name)
        value = endpoint_record(self.state.account.uid, self.state.account.gid, self.network.identity,
                                (resolver.st_dev, resolver.st_ino), self.socket_identity, self.unit.identity.pid)
        return json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n"

    def publish_endpoint(self):
        self.verify()
        self.endpoint_contents = self.endpoint_text()
        directory = trusted_directory(self.directory)
        try:
            try:
                os.stat(self.endpoint.name, dir_fd=directory, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise ServiceError("Refuse an existing private provider endpoint")
            atomic_write(directory, self.endpoint.name, self.endpoint_contents.encode("ascii"))
            info = os.stat(self.endpoint.name, dir_fd=directory, follow_symlinks=False)
            self.endpoint_identity = info.st_dev, info.st_ino
        finally:
            os.close(directory)
        self.verify()

    def verify(self):
        self.state.verify()
        self.dns.services.verify()
        self.unit.verify()
        if self.socket_identity is None or self.socket_node() != self.socket_identity:
            raise ServiceRetained(self.unit)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(1)
            connection.connect(str(self.socket))
            pid, uid, gid = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if (pid != self.unit.identity.pid or uid != 0 or gid != self.files.plan.gid
                    or self.socket_node() != self.socket_identity):
                raise ServiceRetained(self.unit)
        self.unit.verify()
        if self.endpoint_identity is not None:
            directory = trusted_directory(self.directory)
            try:
                info = os.stat(self.endpoint.name, dir_fd=directory, follow_symlinks=False)
                if ((info.st_dev, info.st_ino) != self.endpoint_identity or info.st_nlink != 1
                        or stat.S_IMODE(info.st_mode) != 0o644
                        or root_file(self.endpoint.name, dir_fd=directory) != self.endpoint_contents):
                    raise ServiceRetained(self.unit)
            finally:
                os.close(directory)

    def start(self):
        self.network.verify()
        self.dns.services.verify()
        self.state.prepare()
        return self.start_prepared()

    def start_prepared(self):
        self.state.verify()
        self.dns.services.verify()
        self.unit.start(self.properties(), "service", "run", "--config", str(self.state.path / "config.json"),
                        "--daemon-addr", self.address, "--log-file", "console")
        deadline = time.monotonic() + 8
        while True:
            self.unit.verify()
            try:
                self.socket_identity = self.socket_node()
                break
            except FileNotFoundError:
                if time.monotonic() >= deadline:
                    raise ServiceRetained(self.unit) from None
                time.sleep(.05)
        self.publish_endpoint()
        return self

    def status(self):
        self.verify()
        data = self.network.command.run("/usr/bin/netbird", "--daemon-addr", self.address, "status", "--json")
        value = json.loads(data)
        if not isinstance(value, dict):
            raise ServiceError("Invalid native provider status")
        self.verify()
        return value

    def close(self):
        # This pre-login layer deliberately retains a connected/changed network
        # until the later provider/tunnel journal can prove complete ownership.
        if not self.unit.closed:
            self.state.verify()
            self.dns.services.verify()
            self.unit.close()
        if self.directory.exists():
            raise ServiceRetained(self.unit)
        self.state.release_handles()

    def release_handles(self):
        if self.unit.pidfd is not None:
            self.unit.process.close(self.unit.pidfd)
            self.unit.pidfd = None
        self.state.release_handles()
