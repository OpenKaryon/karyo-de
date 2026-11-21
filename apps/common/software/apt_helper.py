#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Narrow root-side APT entry point for Karyo applications, through pkexec.

No shell, arbitrary files, versions, options, environment or executable paths
are accepted. Root-owned APT configuration and repository trust remain authoritative.
"""
import os
import re
import sys

PACKAGE = re.compile(r"[a-z0-9][a-z0-9+.-]*(?::[a-z0-9][a-z0-9-]{0,31})?", re.ASCII)
ENVIRONMENT = {
    "PATH": "/usr/sbin:/usr/bin:/sbin:/bin",
    "HOME": "/root",
    "LC_ALL": "C.UTF-8",
    "DEBIAN_FRONTEND": "noninteractive",
}
APT = "/usr/bin/apt-get"


def arguments(request):
    if not request:
        raise ValueError("An operation is required.")
    operation, *packages = request
    if operation not in {"install", "remove", "upgrade", "update"}:
        raise ValueError("Unsupported operation.")
    if operation in {"update", "upgrade"} and packages:
        raise ValueError("This operation does not accept package arguments.")
    if operation in {"install", "remove"} and not packages:
        raise ValueError("At least one package is required.")
    if len(packages) > 256 or any(len(name) > 256 or not PACKAGE.fullmatch(name) for name in packages):
        raise ValueError("Not a package name.")
    if operation == "update":
        return [APT, "update"]
    common = [APT, "-y", "-o", "APT::Status-Fd=1", "-o", "Dpkg::Options::=--force-confold"]
    if operation == "upgrade":
        return common + ["full-upgrade"]
    return common + [operation, "--"] + packages


def main(request):
    try:
        argv = arguments(request)
    except ValueError as error:
        print(f"karyo-apt: {error}", file=sys.stderr)
        return 2
    if os.geteuid() != 0:
        print("karyo-apt: administrator authorization is required.", file=sys.stderr)
        return 1
    os.chdir("/")
    os.umask(0o022)
    with open("/dev/null", "rb") as source:
        os.dup2(source.fileno(), 0)
    try:
        os.execve(APT, argv, ENVIRONMENT)
    except OSError as error:
        print(f"karyo-apt: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
