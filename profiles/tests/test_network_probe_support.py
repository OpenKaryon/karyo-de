# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise real UDP loss, delayed responses and the finite recovery budget."""
import socket
import os
from pathlib import Path
import tempfile
import threading
import time
import unittest
from network_probe_support import udp_exchange, write_networkd_dropin


class CarrierDatagrams(unittest.TestCase):
    def test_lost_request_and_delayed_old_reply(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(('127.0.0.1', 0))
            server.settimeout(1)
            endpoint = server.getsockname()
            failures = []
            def respond():
                try:
                    server.recvfrom(100)  # Simulate one lost request during carrier change.
                    request, address = server.recvfrom(100)
                    self.assertEqual(request, b'current nonce')
                    server.sendto(b'previous nonce result', address)
                    server.sendto(b'current result', address)
                except BaseException as error:
                    failures.append(error)
            worker = threading.Thread(target=respond, daemon=True)
            worker.start()
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                result = udp_exchange(client, endpoint, b'current nonce', b'current result', budget=.6, interval=.05)
            worker.join(1)
            self.assertFalse(worker.is_alive())
            self.assertFalse(failures, failures)
            self.assertEqual(result['attempts'], 2)
            self.assertEqual(result['ignored'], 1)

    def test_no_reply_is_not_accepted(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            server.bind(('127.0.0.1', 0))
            started = time.monotonic()
            with self.assertRaisesRegex(TimeoutError, 'No matching UDP payload'):
                udp_exchange(client, server.getsockname(), b'request', b'expected', budget=.15, interval=.04)
            self.assertGreaterEqual(time.monotonic() - started, .13)
            self.assertLess(time.monotonic() - started, 1)

    def test_wrong_payload_does_not_extend_deadline(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(('127.0.0.1', 0));server.settimeout(.1)
            stopped = threading.Event()
            def respond():
                while not stopped.is_set():
                    try:
                        _, address = server.recvfrom(100)
                        server.sendto(b'old payload', address)
                    except socket.timeout:
                        pass
            worker = threading.Thread(target=respond, daemon=True);worker.start()
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
                    with self.assertRaises(TimeoutError):
                        udp_exchange(client, server.getsockname(), b'request', b'expected', budget=.15, interval=.04)
            finally:
                stopped.set();worker.join(1)
            self.assertFalse(worker.is_alive())


@unittest.skipUnless(os.name == 'posix', 'Native service file modes require POSIX')
class NetworkdConfig(unittest.TestCase):
    def test_private_trace_umask_does_not_hide_networkd_configuration(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'link.network.d' / '95-qa.conf'
            previous = os.umask(0o077)
            try:
                created, identity = write_networkd_dropin(path, '[Network]\nDomains=qa.invalid\n')
            finally:
                os.umask(previous)
            self.assertTrue(created)
            self.assertEqual(path.parent.stat().st_mode & 0o777, 0o755)
            self.assertEqual(path.stat().st_mode & 0o777, 0o644)
            self.assertEqual(identity, (path.stat().st_dev, path.stat().st_ino))
            self.assertIn('Domains=qa.invalid', path.read_text())

    def test_existing_unreadable_directory_stops_before_network_update(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'link.network.d' / '95-qa.conf'
            path.parent.mkdir(mode=0o700)
            with self.assertRaisesRegex(PermissionError, 'not readable'):
                write_networkd_dropin(path, '[Network]\n')
            self.assertFalse(path.exists())
            self.assertEqual(path.parent.stat().st_mode & 0o777, 0o700)


if __name__ == '__main__':
    unittest.main()
