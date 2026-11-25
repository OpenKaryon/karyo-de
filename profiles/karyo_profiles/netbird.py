# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned pre-login NetBird service in a profile's network and resolver context.

This primitive does not enable the system NetBird service, import credentials,
or adopt an existing provider. Public startup combines this scope with the
captured tunnel, graphical owner and durable host journal.
"""
import json
import os
from pathlib import Path
import socket
import stat
import struct
import time

from .model import root_file
from .netbird_client import endpoint_record
from .provider_state import ProviderState, STATE_ROOT
from .services import OwnedUnit, ServiceError, ServiceRetained, host_controller, program_identity
from .storage import atomic_write, trusted_directory
from .switching import ProviderUnavailable

def netbird_ready():
    """Require trusted optional payload before allocating any profile resource."""
    from .catalog import require_netbird_catalog
    try:
        for program in ("/usr/bin/netbird", "/usr/bin/netbird-ui",
                        "/usr/bin/karyo-netbird-cli", "/usr/bin/karyo-netbird-gui", "/usr/bin/karyo-netbird-context",
                        "/usr/libexec/karyo/netbird-opener/xdg-open"):
            program_identity(program)
        require_netbird_catalog()
    except (OSError, ValueError, ServiceError) as error:
        raise ProviderUnavailable() from error


class NetBirdState(ProviderState):
    def __init__(self, files, root=STATE_ROOT):
        super().__init__(files, "netbird", root)


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
        # The journal declares namespace-local tunnel ownership before startup.
        # Keep host-veth identity and consumer checks while the provider stops.
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
