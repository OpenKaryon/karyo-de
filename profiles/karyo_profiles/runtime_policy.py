# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit administrator opt-in for private desktop networking.

Installing the runtime payload does not enable forwarding, start a controller,
or change the host resolver. The administrator must configure resolved first.
"""
import argparse
import os
from pathlib import Path
import stat
import sys

from .dns import discover_dns, resolver_service_uid, supported_resolver_layout
from .model import root_file
from .networks import NetworkCommand
from .services import program_identity, host_controller
from .storage import ConfigStore, atomic_write, trusted_directory
from .underlay import discover_underlay

POLICY = Path("/etc/karyo/profile-runtime.conf")
ENABLED = "version=1\nisolated=enabled\n"
FORWARDING = Path("/proc/sys/net/ipv4/ip_forward")
NETWORK_UNIT = "karyo-profile-networking.service"
SOCKET_UNIT = "karyo-profile-host.socket"
EXECUTABLES = (
    "/usr/bin/systemctl", "/usr/bin/systemd-run", "/usr/bin/busctl", "/usr/bin/loginctl",
    "/usr/bin/setpriv", "/usr/sbin/ip", "/usr/sbin/nft", "/usr/sbin/sysctl",
    "/usr/bin/dbus-daemon", "/usr/lib/systemd/systemd-resolved",
    "/usr/libexec/karyo/profile-session-entry", "/usr/libexec/karyo/profile-session-wait",
    "/usr/bin/karyo-session", "/usr/bin/labwc",
)


def enabled(path=POLICY):
    directory = None
    try:
        directory = trusted_directory(path.parent)
        value = root_file(path.name, dir_fd=directory)
        info = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
        if info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o600:
            raise PermissionError("The desktop runtime policy must be a private root-owned file")
        if value != ENABLED:
            raise ValueError("Unsupported desktop runtime policy; administrator review is required")
        return True
    except FileNotFoundError:
        return False
    finally:
        if directory is not None:
            os.close(directory)


def forwarding_ready():
    if FORWARDING.read_text().strip() != "1":
        raise RuntimeError("Private desktops require explicitly enabled IPv4 forwarding")


def dependencies(command):
    host_controller()
    for program in EXECUTABLES:
        program_identity(program)
    # The fixed PAM entry never authenticates a password and must be installed
    # separately from the public account helpers.
    pam = root_file("/etc/pam.d/karyo-desktop")
    if pam != ("# SPDX-License-Identifier: GPL-3.0-or-later\n"
               "# The authorized root controller opens only account/session management.\n"
               "# This stack cannot be used as a password authentication bypass.\n"
               "auth requisite pam_deny.so\n@include common-account\n@include common-session\n"):
        raise RuntimeError("The controlled desktop PAM entry needs administrator review")
    supported_resolver_layout()
    resolver_service_uid()
    for unit in (NETWORK_UNIT, SOCKET_UNIT):
        state = command.run("/usr/bin/systemctl", "show", "--property=LoadState", "--value", unit).strip()
        if state != "loaded":
            raise RuntimeError("The optional desktop runtime units are not installed")
    # Fail before publishing policy if the physical DNS backend is unavailable.
    discover_dns(discover_underlay(command), command)


def apply(command=None):
    command = NetworkCommand() if command is None else command
    host_controller()
    if not enabled():
        raise PermissionError("Private desktop networking has not been enabled by the administrator")
    dependencies(command)
    command.run("/usr/sbin/sysctl", "-w", "net.ipv4.ip_forward=1")
    forwarding_ready()
    # No stop hook resets a global shared sysctl: other VPN/container software
    # may depend on it, and its later changes do not belong to this service.


def enable(command=None, store=None):
    command = NetworkCommand() if command is None else command
    store = ConfigStore() if store is None else store
    host_controller()
    with store.locked():
        dependencies(command)
        if not enabled():
            directory = trusted_directory(POLICY.parent, create=True)
            try:
                atomic_write(directory, POLICY.name, ENABLED.encode("ascii"), mode=0o600)
            finally:
                os.close(directory)
    # Persist the administrator's explicit choice across boots. A failed native
    # activation is reported; it is never disguised as a complete rollback.
    command.run("/usr/bin/systemctl", "daemon-reload")
    command.run("/usr/bin/systemctl", "enable", "--now", NETWORK_UNIT)
    forwarding_ready()
    command.run("/usr/bin/systemctl", "enable", "--now", SOCKET_UNIT)
    return {"isolated_desktops": "enabled"}


def main():
    parser = argparse.ArgumentParser(prog="karyo-profile-runtime", allow_abbrev=False)
    parser.add_argument("operation", choices=("enable", "apply"))
    args = parser.parse_args()
    try:
        if args.operation == "enable":
            enable()
            print("Private desktop runtime enabled. Home keeps its ordinary network.")
        else:
            apply()
        return 0
    except (OSError, RuntimeError, ValueError):
        print("karyo-profile-runtime: setup did not finish; review the runtime prerequisites and unit journal.", file=sys.stderr)
        return 1
