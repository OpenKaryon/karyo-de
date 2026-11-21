# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import socket
import struct
import sys
import unittest
from unittest.mock import MagicMock, Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccount
from karyo_profiles.dispatcher import HostDispatcher, operation, receive, send
from karyo_profiles.logind import Session
from karyo_profiles.peer import DesktopPeer
from karyo_profiles.switching import DesktopStopped

HOME = LocalAccount("home", 1001, 1001, "/home/home", "/bin/bash")
LOGIN = Session("c1", "home", 1001, 8, True)


class PeerTest(unittest.TestCase):
    def setUp(self):
        self.connection = Mock()
        self.connection.getsockopt.return_value = struct.pack("3i", 1234, 0, 0)
        self.accounts = Mock()
        self.accounts.by_uid.return_value = HOME
        self.sessions = Mock()
        self.sessions.get.return_value = LOGIN
        self.process = Mock()
        self.process.open.return_value = 17
        self.process.exited.return_value = False
        self.process.read.return_value = 567
        self.identify = Mock(return_value="c1")
        self.program = patch("karyo_profiles.peer.program_identity", return_value=(2, 3))
        self.namespace = patch.object(DesktopPeer, "host_user_namespace")
        self.program.start(); self.namespace.start()
        self.addCleanup(self.program.stop); self.addCleanup(self.namespace.stop)

    def make(self):
        return DesktopPeer(self.connection, accounts=self.accounts, sessions=self.sessions,
                           process=self.process, identify=self.identify)

    def test_kernel_peer_owns_captured_login_without_forwarded_environment(self):
        peer = self.make()
        self.assertEqual(peer.verify(), HOME)
        self.identify.assert_called_with(1234)
        self.process.read.assert_called_with(1234, (2, 3), 0,
            "/user.slice/user-1001.slice/session-c1.scope", None)
        peer.close(); peer.close()
        self.process.close.assert_called_once_with(17)
        with self.assertRaises(PermissionError): peer.verify()

    def test_unprivileged_user_group_or_missing_pid_never_opens_pidfd(self):
        for credentials in ((1234,1001,1001), (1234,0,1001), (0,0,0), (1,0,0)):
            with self.subTest(credentials=credentials):
                self.connection.getsockopt.return_value = struct.pack("3i", *credentials)
                with self.assertRaises(PermissionError): self.make()
        self.process.open.assert_not_called()

    def test_inactive_missing_or_inconsistent_login_releases_pidfd(self):
        for login in (None, Session("c1","home",1001,8,False), Session("c1","other",1001,8,True)):
            with self.subTest(login=login):
                self.sessions.get.return_value=login
                with self.assertRaises(PermissionError): self.make()
        self.assertEqual(self.process.close.call_count,3)

    def test_pid_reuse_exit_scope_move_uid_or_account_changes_are_rejected(self):
        changes = ((self.process.exited, True), (self.process.read, 999),
                   (self.identify, "c2"), (self.sessions.get, None),
                   (self.sessions.get, Session("c1","home",1001,9,True)),
                   (self.sessions.get, Session("c1","home",1001,8,False)),
                   (self.accounts.by_uid, LocalAccount("other",1001,1001,"/home/other","/bin/bash")))
        for mocked, value in changes:
            with self.subTest(value=value):
                peer=self.make(); previous=mocked.return_value; mocked.return_value=value
                try:
                    with self.assertRaises(PermissionError): peer.verify()
                finally:
                    mocked.return_value=previous; peer.close()

    def test_changed_credentials_executable_or_cgroup_errors_close_capture(self):
        self.process.read.side_effect=PermissionError("native mismatch")
        with self.assertRaises(PermissionError): self.make()
        self.process.close.assert_called_once_with(17)


class ProtocolTest(unittest.TestCase):
    def pair(self):
        left,right=socket.socketpair()
        self.addCleanup(left.close); self.addCleanup(right.close)
        return left,right

    def frame(self,data,declared=None):
        left,right=self.pair()
        left.sendall(struct.pack("!I",len(data) if declared is None else declared)+data)
        left.shutdown(socket.SHUT_WR)
        return receive(right,4096,.1)

    def test_bounded_utf8_frame_and_write_half_close(self):
        left,right=self.pair()
        send(left,{"version":1,"operation":"list"},4096,1)
        self.assertEqual(operation(receive(right,4096,1)),("list",None))
        self.assertEqual(self.frame('{"version":1,"operation":"switch","name":"work"}'.encode()),
                         {"version":1,"operation":"switch","name":"work"})

    def test_duplicate_fields_invalid_utf8_truncation_oversize_and_trailing_frame(self):
        packets=[(b'{"version":1,"version":1}',None),(b'\xff',None),(b'{}',0),
                 (b'{}',4097),(b'{}',1),(b'{}',3),(b'{}{}',2)]
        for data,size in packets:
            with self.subTest(data=data,size=size):
                with self.assertRaises((ValueError,UnicodeError)):self.frame(data,size)

    def test_slow_or_unclosed_request_has_one_bounded_deadline(self):
        left,right=self.pair()
        left.sendall(struct.pack("!I",2)+b'{}')
        with self.assertRaises(TimeoutError):receive(right,4096,.01)

    def test_only_fixed_operations_accept_no_forwarded_identity_or_commands(self):
        packets=[{},[],{"version":True,"operation":"list"},{"version":2,"operation":"list"},
            {"version":1,"operation":"start"}, {"version":1,"operation":"list","uid":1001},
            {"version":1,"operation":"list","session":"c1"},
            {"version":1,"operation":"switch","name":"../work"},
            {"version":1,"operation":"switch","name":"work","program":"/bin/sh"}]
        for packet in packets:
            with self.subTest(packet=packet):
                with self.assertRaises(ValueError):operation(packet)

    def dispatcher(self, *, peer=None, switcher=None):
        store=Mock()
        from contextlib import nullcontext
        store.locked.return_value=nullcontext(store)
        guard=Mock(); caller=Mock();caller.verify.return_value=HOME
        factory=Mock(return_value=caller) if peer is None else peer
        controller=HostDispatcher(store,accounts=Mock(),sessions=Mock(),peer=factory,
                                  switcher=Mock() if switcher is None else switcher,guard=guard)
        return controller,caller,guard

    def call(self,controller,packet):
        client,server=self.pair()
        send(client,packet,4096,1)
        controller.handle(server)
        return receive(client,65536,1)

    def test_authenticated_family_listing_and_no_duplicate_operation(self):
        controller,caller,guard=self.dispatcher()
        with patch("karyo_profiles.dispatcher.listing",return_value=[{"user":"home"}]) as listing:
            result=self.call(controller,{"version":1,"operation":"list"})
            self.assertEqual(result,{"version":1,"result":[{"user":"home"}]})
            self.assertEqual(listing.call_args.args[0],HOME)
            listing.assert_called_once()
        caller.verify.assert_called_once();caller.close.assert_called_once()
        self.assertEqual(guard.call_count,2)

    def test_switch_uses_only_recaptured_caller_and_validated_name(self):
        switcher=Mock();switcher.return_value.switch.return_value={"user":"work","vt":9,"changed":True}
        controller,caller,guard=self.dispatcher(switcher=switcher)
        result=self.call(controller,{"version":1,"operation":"switch","name":"work"})
        self.assertTrue(result["result"]["changed"])
        switcher.return_value.switch.assert_called_once_with(HOME,"work",reauthorize=caller.verify)

    def test_lost_ack_never_repeats_switch(self):
        switcher=Mock();switcher.return_value.switch.return_value={"changed":True}
        controller,caller,guard=self.dispatcher(switcher=switcher)
        client,server=self.pair()
        send(client,{"version":1,"operation":"switch","name":"work"},4096,1);client.close()
        controller.handle(server)
        switcher.return_value.switch.assert_called_once_with(HOME,"work",reauthorize=caller.verify)
        caller.close.assert_called_once()

    def test_errors_are_typed_sanitized_and_peer_always_closed(self):
        for error,code in ((PermissionError("secret"),"denied"),(DesktopStopped("secret"),"stopped"),
                           (RuntimeError("secret"),"failed"),(ValueError("secret"),"invalid")):
            with self.subTest(code=code):
                controller,caller,guard=self.dispatcher();caller.verify.side_effect=error
                self.assertEqual(self.call(controller,{"version":1,"operation":"list"}),{"version":1,"error":code})
                caller.close.assert_called_once()

    def test_bad_packet_cannot_invoke_verified_account_operation(self):
        controller,caller,guard=self.dispatcher()
        with patch("karyo_profiles.dispatcher.listing") as listing:
            self.assertEqual(self.call(controller,{"version":1,"operation":"list","owner":"root"})["error"],"invalid")
            listing.assert_not_called();caller.verify.assert_not_called()
        caller.close.assert_called_once()


class SocketActivationTest(unittest.TestCase):
    def test_foreign_activation_arguments_rejected_before_using_fd(self):
        from karyo_profiles.dispatcher import activated_listener
        valid={"LISTEN_PID":str(os.getpid()),"LISTEN_FDS":"1","LISTEN_FDNAMES":"profiles"}
        with patch("karyo_profiles.dispatcher.host_controller"), patch("os.set_inheritable") as fd:
            for changes in ({"LISTEN_PID":"1"},{"LISTEN_FDS":"2"},{"LISTEN_FDNAMES":"other"}):
                with self.assertRaises(PermissionError): activated_listener(valid|changes)
            fd.assert_not_called()

    def test_only_fixed_native_listening_stream_is_accepted(self):
        from karyo_profiles.dispatcher import ADDRESS, activated_listener
        environment={"LISTEN_PID":str(os.getpid()),"LISTEN_FDS":"1","LISTEN_FDNAMES":"profiles"}
        native=Mock()
        native.getsockopt.side_effect=[socket.AF_UNIX,socket.SOCK_STREAM,1]
        native.getsockname.return_value=ADDRESS
        with patch("karyo_profiles.dispatcher.host_controller"),patch("os.set_inheritable") as inheritable, \
             patch("os.dup",return_value=33),patch("os.close") as close, \
             patch("karyo_profiles.dispatcher.socket.socket",return_value=native), \
             patch("karyo_profiles.dispatcher.socket_node") as node:
            self.assertIs(activated_listener(environment),native)
            inheritable.assert_called_once_with(3,False);close.assert_called_once_with(3)
            node.assert_called_once();self.assertEqual(environment,{})
            native.close.assert_not_called()

    def test_wrong_domain_type_accept_state_or_path_closes_duplicate_only(self):
        from karyo_profiles.dispatcher import ADDRESS, activated_listener
        for domain,kind,accept,path in ((socket.AF_INET,socket.SOCK_STREAM,1,ADDRESS),
            (socket.AF_UNIX,socket.SOCK_DGRAM,1,ADDRESS),(socket.AF_UNIX,socket.SOCK_STREAM,0,ADDRESS),
            (socket.AF_UNIX,socket.SOCK_STREAM,1,"/tmp/other")):
            environment={"LISTEN_PID":str(os.getpid()),"LISTEN_FDS":"1","LISTEN_FDNAMES":"profiles"}
            native=Mock();native.getsockopt.side_effect=[domain,kind,accept];native.getsockname.return_value=path
            with patch("karyo_profiles.dispatcher.host_controller"),patch("os.set_inheritable"), \
                 patch("os.dup",return_value=33),patch("os.close") as close, \
                 patch("karyo_profiles.dispatcher.socket.socket",return_value=native):
                with self.assertRaises(PermissionError):activated_listener(environment)
                native.close.assert_called_once();close.assert_not_called()

    def test_client_refuses_unprivileged_connection_before_socket_lookup(self):
        from karyo_profiles.dispatcher import request
        with patch("os.getuid",return_value=1001),patch("karyo_profiles.dispatcher.socket_node") as node:
            with self.assertRaises(PermissionError):request({"version":1,"operation":"list"})
            node.assert_not_called()

    def test_socket_activation_client_accepts_root_pid_one_but_not_foreign_owner(self):
        from karyo_profiles.dispatcher import request
        for pid,uid,gid,allowed in ((1,0,0,True),(456,0,0,True),(0,0,0,False),(456,1001,1001,False)):
            native=MagicMock();connection=native.__enter__.return_value
            connection.getsockopt.return_value=struct.pack("3i",pid,uid,gid)
            with patch("os.getuid",return_value=0),patch("os.geteuid",return_value=0), \
                 patch("karyo_profiles.dispatcher.socket_node",return_value=(1,2)), \
                 patch("karyo_profiles.dispatcher.socket.socket",return_value=native), \
                 patch("karyo_profiles.dispatcher.send") as send, \
                 patch("karyo_profiles.dispatcher.receive",return_value={"version":1,"result":[]}):
                if allowed:self.assertEqual(request({"version":1,"operation":"list"}),[])
                else:
                    with self.assertRaises(PermissionError):request({"version":1,"operation":"list"})
                    send.assert_not_called()

    def test_remote_process_session_rejects_noncanonical_pid_before_library(self):
        from karyo_profiles.logind import process_session
        with patch("karyo_profiles.logind.ctypes.CDLL") as library:
            for value in (True, -1, 2**31, "1234", None):
                with self.assertRaises(ValueError):process_session(value)
            library.assert_not_called()


if __name__ == "__main__": unittest.main()
