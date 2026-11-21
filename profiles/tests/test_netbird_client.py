# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import io
import json
import os
from pathlib import Path
import stat
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles import netbird_client as client

PATH = "/run/karyo-netbird-" + "ab" * 16 + "/client.json"


class EndpointTest(unittest.TestCase):
    def setUp(self):
        self.value = client.endpoint_record(1002, 1002, (4, 5), (12, 13), (6, 7, 8, 9), 123)

    def test_only_exact_typed_endpoint_schema_is_accepted(self):
        client.validate(self.value)
        for key, value in (("version", True), ("uid", True), ("gid", -1), ("pid", 1),
                           ("network", [4]), ("socket", [6, 7, 8, False]), ("program", "/bin/sh")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                client.validate(dict(self.value, **{key: value}))
        with self.assertRaises(ValueError):
            json.loads('{"version":1,"version":1}', object_pairs_hook=client.unique)

    def test_untrusted_path_and_root_identity_never_open_metadata(self):
        for path in (None, "/tmp/client.json", PATH + "x", PATH.replace("ab", "AB"), PATH + "/../client.json"):
            with self.subTest(path=path), patch.object(client, "trusted_directory") as open_directory:
                with self.assertRaises(ValueError): client.capture(path)
                open_directory.assert_not_called()
        with patch.object(client, "trusted_directory") as open_directory, self.assertRaises(PermissionError):
            with patch.object(client.os, "getuid", return_value=0): client.capture(PATH)
        open_directory.assert_not_called()

    def capture(self, *, network=(4, 5), resolver=(12, 13), peer=(123, 0, 1002), endpoint=None, changed_socket=False):
        info = SimpleNamespace(st_dev=10, st_ino=11, st_nlink=1, st_mode=stat.S_IFREG | 0o644, st_size=200)
        directory = SimpleNamespace(st_dev=6, st_ino=7, st_gid=1002, st_mode=stat.S_IFDIR | 0o750)
        node = SimpleNamespace(st_dev=8, st_ino=9, st_nlink=1, st_uid=0, st_gid=1002, st_mode=stat.S_IFSOCK | 0o660)
        def native_stat(path, **options):
            if path == "/proc/self/ns/net": return SimpleNamespace(st_dev=network[0], st_ino=network[1])
            if path == "/run/systemd/resolve": return SimpleNamespace(st_dev=resolver[0], st_ino=resolver[1])
            if path == "control.sock":
                if changed_socket: node.st_ino += 1
                return node
            return info
        server = Mock()
        server.__enter__ = Mock(return_value=server)
        server.__exit__ = Mock(return_value=False)
        server.getsockopt.return_value = struct.pack("3i", *peer)
        with contextlib.ExitStack() as stack:
            for name in ("getuid", "geteuid", "getgid", "getegid"):
                stack.enter_context(patch.object(client.os, name, return_value=1002))
            stack.enter_context(patch.object(client, "trusted_directory", return_value=55))
            stack.enter_context(patch.object(client.os, "fstat", return_value=directory))
            stack.enter_context(patch.object(client.os, "stat", side_effect=native_stat))
            stack.enter_context(patch.object(client.os, "close"))
            stack.enter_context(patch.object(client, "root_file", return_value=json.dumps(endpoint or self.value)))
            stack.enter_context(patch.object(client.socket, "socket", return_value=server))
            return client.capture(PATH)

    def test_matching_root_endpoint_resolves_to_fixed_private_socket(self):
        self.assertEqual(self.capture(), "unix://" + PATH.removesuffix("client.json") + "control.sock")

    def test_wrong_user_namespace_peer_or_replaced_socket_is_refused(self):
        for options in ({"network": (4, 50)}, {"resolver": (12, 14)}, {"peer": (124, 0, 1002)}, {"peer": (123, 1002, 1002)},
                        {"endpoint": dict(self.value, uid=1001)}, {"changed_socket": True}):
            with self.subTest(options=options), self.assertRaises(PermissionError): self.capture(**options)


class ClientLaunchTest(unittest.TestCase):
    def test_cli_gui_clear_inherited_caps_and_choose_the_same_verified_daemon(self):
        for kind, program, flag in (("cli", "netbird", "--daemon-addr"), ("gui", "netbird-ui", "-daemon-addr")):
            read = Mock(return_value="unix:///run/private/control.sock")
            args, env = client.command(kind, ["status"] if kind == "cli" else [],
                                       {"KARYO_NETBIRD_ENDPOINT": PATH, "NB_DAEMON_ADDR": "unix:///wrong.sock",
                                        "DISPLAY": ":fixture", "PATH": "/untrusted"}, read=read)
            read.assert_called_once_with(PATH)
            self.assertEqual(args[:5], ["/usr/bin/setpriv", "--inh-caps=-all", "--ambient-caps=-all", "--", "/usr/bin/" + program])
            self.assertEqual(args[5:7], [flag, "unix:///run/private/control.sock"])
            self.assertEqual(env["NB_DAEMON_ADDR"], args[6])
            self.assertEqual(env["PATH"], client.CLIENT_PATH)
            self.assertEqual(env["DISPLAY"], ":fixture")
            self.assertNotIn("--no-new-privs", args)
            self.assertFalse(any(arg.startswith("--bounding-set") for arg in args))

    def test_gui_preserves_sandbox_despite_vendor_heuristic_or_inherited_opt_out(self):
        args, env = client.command("gui", [], {"WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS": "1"},
                                   read=lambda path: "unix:///run/private/control.sock")
        self.assertEqual(env["WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS"], "0")
        self.assertNotIn("WEBKIT_FORCE_SANDBOX", env)
        self.assertEqual(args[0], "/usr/bin/setpriv")

    def test_client_cannot_override_daemon_and_never_uses_a_shell(self):
        for args in (["--daemon-addr=unix:///other"], ["-daemon-addr", "unix:///other"], ["bad\nargument"]):
            read = Mock()
            with self.assertRaises(ValueError): client.command("gui", args, {}, read=read)
            read.assert_not_called()
        with self.assertRaises(ValueError): client.command("root", [], {}, read=Mock())

    def test_browser_handoff_spawns_without_waiting_for_cold_browser(self):
        spawn = Mock()
        url = "https://auth.invalid/start?state=synthetic-fixture"
        self.assertEqual(client.open_browser([url], spawn=spawn), 0)
        args, options = spawn.call_args
        self.assertEqual(args, (["/usr/bin/xdg-open", url],))
        self.assertTrue(options["close_fds"] and options["start_new_session"])
        self.assertNotIn("shell", options)
        spawn.return_value.wait.assert_not_called()
        spawn.return_value.communicate.assert_not_called()

    def test_browser_rejects_non_urls_controls_and_multiple_arguments(self):
        for args in ([], ["--help"], ["file:///tmp/fixture"], ["javascript:fixture"],
                     ["https://example.org/\n"], ["https://example.org/", "other"]):
            spawn = Mock()
            with self.subTest(args=args), self.assertRaises(ValueError): client.open_browser(args, spawn=spawn)
            spawn.assert_not_called()

    def test_opener_error_does_not_log_url_or_auth_code(self):
        with patch.object(client.sys, "argv", ["xdg-open", "https://auth.invalid/?code=fixture"]), \
                patch.object(client, "open_browser", side_effect=OSError("fixture code")), \
                contextlib.redirect_stderr(io.StringIO()) as stderr:
            self.assertEqual(client.opener_main(), 1)
        self.assertEqual(stderr.getvalue(), "")
