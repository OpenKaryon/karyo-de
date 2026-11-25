# SPDX-License-Identifier: GPL-3.0-or-later
"""Unprivileged provider clients bound to their root-published desktop context."""
import json
import os
from pathlib import Path
import re
import socket
import stat
import struct
import subprocess
import sys
from urllib.parse import urlsplit

from .model import root_file
from .storage import trusted_directory

ENDPOINT = re.compile(r"/run/karyo-netbird-[a-f0-9]{32}/client\.json")
CLIENT_PATH = "/usr/libexec/karyo/netbird-opener:/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin"


def positive(value, size):
    return type(value) is int and 0 < value < size


def endpoint_record(uid, gid, network, resolver, socket_identity, pid):
    value = {"version": 1, "uid": uid, "gid": gid, "network": list(network),
             "resolver": list(resolver), "socket": list(socket_identity), "pid": pid}
    validate(value)
    return value


def validate(value):
    if (not isinstance(value, dict) or set(value) != {"version", "uid", "gid", "network", "resolver", "socket", "pid"}
            or type(value["version"]) is not int or value["version"] != 1
            or type(value["uid"]) is not int or not 1000 <= value["uid"] < 17384
            or type(value["gid"]) is not int or not 0 <= value["gid"] < 2**32
            or not positive(value["pid"], 2**31) or value["pid"] == 1):
        raise ValueError("Invalid private provider endpoint")
    for name, size in (("network", 2), ("resolver", 2), ("socket", 4)):
        parts = value[name]
        if not isinstance(parts, list) or len(parts) != size or not all(positive(v, 2**64) for v in parts):
            raise ValueError("Invalid private provider native identity")
    return value


def unique(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("Duplicate private provider endpoint field")
        value[key] = item
    return value


def capture(path):
    if not isinstance(path, str) or ENDPOINT.fullmatch(path) is None:
        raise ValueError("Start the provider client from its Karyo desktop")
    uid, gid = os.getuid(), os.getgid()
    if uid != os.geteuid() or gid != os.getegid() or not 1000 <= uid < 17384:
        raise PermissionError("The provider client requires an ordinary desktop user")
    path = Path(path)
    directory = trusted_directory(path.parent)
    try:
        parent = os.fstat(directory)
        info = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
        if (parent.st_gid != gid or stat.S_IMODE(parent.st_mode) != 0o750
                or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o644 or info.st_size > 4096):
            raise PermissionError("Unsafe private provider endpoint metadata")
        text = root_file(path.name, dir_fd=directory)
        value = validate(json.loads(text, object_pairs_hook=unique))
        if value["uid"] != uid or value["gid"] != gid:
            raise PermissionError("The provider endpoint belongs to another desktop")
        current = os.stat("/proc/self/ns/net")
        if [current.st_dev, current.st_ino] != value["network"]:
            raise PermissionError("The provider client is outside its desktop network")
        resolver = os.stat("/run/systemd/resolve")
        if [resolver.st_dev, resolver.st_ino] != value["resolver"]:
            raise PermissionError("The provider client is outside its desktop resolver context")
        node = os.stat("control.sock", dir_fd=directory, follow_symlinks=False)
        identity = [parent.st_dev, parent.st_ino, node.st_dev, node.st_ino]
        if (identity != value["socket"] or not stat.S_ISSOCK(node.st_mode)
                or node.st_uid != 0 or node.st_gid != gid or node.st_nlink != 1):
            raise PermissionError("The private provider socket changed")
        address = path.parent / "control.sock"
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(1)
            connection.connect(str(address))
            peer = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if peer != (value["pid"], 0, gid):
                raise PermissionError("The private provider socket owner changed")
        after = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
        after_node = os.stat("control.sock", dir_fd=directory, follow_symlinks=False)
        if ((after.st_dev, after.st_ino) != (info.st_dev, info.st_ino)
                or (after_node.st_dev, after_node.st_ino) != (node.st_dev, node.st_ino)
                or root_file(path.name, dir_fd=directory) != text):
            raise PermissionError("The provider endpoint changed during launch")
        return "unix://" + str(address)
    finally:
        os.close(directory)


def command(kind, arguments, environment, *, read=capture):
    if kind not in {"cli", "gui"}:
        raise ValueError("Unsupported provider client")
    if len(arguments) > 128 or any(not isinstance(v, str) or len(v) > 16384 or any(c in v for c in "\0\r\n")
                                   for v in arguments):
        raise ValueError("Invalid provider client arguments")
    if any(v.split("=", 1)[0] in {"--daemon-addr", "-daemon-addr"} for v in arguments):
        raise ValueError("The provider daemon address is fixed by the desktop")
    address = read(environment.get("KARYO_NETBIRD_ENDPOINT"))
    program, flag = (("/usr/bin/netbird", "--daemon-addr") if kind == "cli"
                     else ("/usr/bin/netbird-ui", "-daemon-addr"))
    env = dict(environment, PATH=CLIENT_PATH, NB_DAEMON_ADDR=address)
    if kind == "gui":
        # NetBird 0.82 may disable WebKit merely because Ubuntu restricts userns,
        # even when the native bubblewrap policy permits the renderer sandbox.
        # Its bootstrap respects an explicit value; WebKit treats 0 as enabled.
        env["WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS"] = "0"
        env["WEBKIT_DISABLE_DMABUF_RENDERER"] = "1"  # Preserve the upstream desktop launch default.
    args = ["/usr/bin/setpriv", "--inh-caps=-all", "--ambient-caps=-all", "--", program,
            flag, address, *arguments]
    return args, env


def session_context(environment, *, query=None):
    """Recover only the provider routing hint from this user's native manager."""
    value = environment.get('KARYO_NETBIRD_ENDPOINT')
    if value is None:
        from .network_client import manager_environment
        from .networks import NetworkError
        try:values = (manager_environment if query is None else query)()
        except (OSError, NetworkError):return ''
        matches = [item.split('=', 1)[1] for item in values if item.startswith('KARYO_NETBIRD_ENDPOINT=')]
        if not matches:return ''
        if len(matches) != 1:raise ValueError('Ambiguous private provider routing hint')
        value = matches[0]
    if not isinstance(value, str) or ENDPOINT.fullmatch(value) is None:
        raise ValueError('Invalid private provider routing hint')
    return value


def main(kind):
    try:
        if kind == 'context':
            if sys.argv[1:]:raise ValueError('The provider context helper accepts no arguments')
            path = session_context(os.environ)
            if path:print(path)
            return 0
        args, env = command(kind, sys.argv[1:], os.environ)
        os.execve(args[0], args, env)
    except (OSError, ValueError):
        print("karyo-netbird: open this client from its desktop; its private service is unavailable or changed.",
              file=sys.stderr)
        return 1


def open_browser(arguments, *, spawn=subprocess.Popen):
    if len(arguments) != 1 or len(arguments[0]) > 16384 or any(ord(c) < 32 or ord(c) == 127 for c in arguments[0]):
        raise ValueError("The provider browser handoff needs one bounded URL")
    value = urlsplit(arguments[0])
    if value.scheme not in {"http", "https"} or not value.hostname:
        raise ValueError("The provider browser handoff accepts only HTTP(S)")
    # Waiting for a cold browser here prevents NetBird from opening its local
    # PKCE listener. Hand off in the same ordinary-user desktop context instead.
    spawn(["/usr/bin/xdg-open", arguments[0]], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
          stderr=subprocess.DEVNULL, close_fds=True, start_new_session=True)
    return 0


def opener_main():
    try:
        return open_browser(sys.argv[1:])
    except (OSError, ValueError):
        return 1  # Authentication URLs/codes never enter diagnostics.
