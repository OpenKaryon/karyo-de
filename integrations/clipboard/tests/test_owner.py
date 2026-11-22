# SPDX-License-Identifier: GPL-3.0-or-later
"""Real X11 clipboard protocol checks in an isolated Xvfb server."""
import os
from pathlib import Path
import select
import stat
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(os.environ.get('KARYO_XCLIPBOARD_TEST_MODULES', str(Path(__file__).resolve().parents[1])))
OWNER = ROOT / 'karyo_xclipboard/owner.py'
PNG = 'image/png'


@unittest.skipUnless(sys.platform != 'win32' and os.getuid()!=0 and shutil.which('Xvfb') and shutil.which('xclip'), 'Needs Linux Xvfb and xclip')
class ClipboardOwnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        read_fd, write_fd = os.pipe()
        cls.server = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0', '800x600x24', '-nolisten', 'tcp'],
                                      pass_fds=(write_fd,), stderr=subprocess.DEVNULL)
        os.close(write_fd)
        try:
            if not select.select([read_fd], [], [], 5)[0]:
                raise RuntimeError('Xvfb did not start')
            number = os.read(read_fd, 32).decode().strip()
        finally:
            os.close(read_fd)
        cls.env = {**os.environ, 'DISPLAY': ':' + number}
        cls.env.pop('XAUTHORITY', None)

    @classmethod
    def tearDownClass(cls):
        cls.server.terminate()
        cls.server.wait(timeout=5)

    def setUp(self):
        self.owners = []

    def tearDown(self):
        for owner in self.owners:
            if owner.poll() is None:
                owner.terminate()
            owner.wait(timeout=5)
            owner.stdout.close()

    def publish(self, mime, data):
        owner = subprocess.Popen([sys.executable, str(OWNER), mime], env=self.env, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.owners.append(owner)
        owner.stdin.write(data)
        owner.stdin.close()
        self.assertTrue(select.select([owner.stdout], [], [], 5)[0], 'Owner readiness timeout')
        self.assertEqual(owner.stdout.readline().split()[0], b'READY')
        return owner

    def query(self, target):
        return subprocess.check_output(['xclip', '-out', '-selection', 'clipboard', '-target', target], env=self.env, stderr=subprocess.DEVNULL, timeout=8)

    def timestamp(self):
        # xclip prints format-32 properties as decimal text.
        return int(self.query('TIMESTAMP').strip())

    def test_png_has_timestamp_and_survives_many_paste_requests(self):
        data = b'\x89PNG\r\n\x1a\n' + bytes(range(256)) * 8000
        self.publish(PNG, data)
        self.assertIn(b'TIMESTAMP', self.query('TARGETS').splitlines())
        stamp = self.timestamp()
        self.assertGreater(stamp, 0)
        for _ in range(3):
            self.assertEqual(self.query(PNG), data)  # INCR exceeds the X request limit.
            self.assertEqual(self.timestamp(), stamp)

    def test_new_copy_is_newer_than_older_primary_without_replacing_it(self):
        from Xlib import X, Xatom, display
        connection = display.Display(self.env['DISPLAY'])
        try:
            screen = connection.screen()
            window = screen.root.create_window(0, 0, 1, 1, 0, screen.root_depth, X.InputOutput, X.CopyFromParent,
                                               event_mask=X.PropertyChangeMask)
            clock = connection.intern_atom('TEST_CLOCK')
            window.change_property(clock, Xatom.INTEGER, 32, [0]); connection.flush()
            old_stamp = connection.next_event().time
            primary = connection.intern_atom('PRIMARY')
            window.set_selection_owner(primary, old_stamp); connection.sync()
            time.sleep(.01)
            old_owner = self.publish(PNG, b'\x89PNG\r\n\x1a\nfirst')
            first_stamp = self.timestamp()
            self.assertGreater(first_stamp, old_stamp)
            self.assertEqual(connection.get_selection_owner(primary).id, window.id)
            time.sleep(.01)
            self.publish(PNG, b'\x89PNG\r\n\x1a\nsecond')
            self.assertGreater(self.timestamp(), first_stamp)
            self.assertEqual(self.query(PNG), b'\x89PNG\r\n\x1a\nsecond')
            old_owner.wait(timeout=3)
            self.assertEqual(connection.get_selection_owner(primary).id, window.id)
        finally:
            connection.close()

    def test_unicode_text_has_the_same_timestamp_contract(self):
        text = 'Копирование текста\nSecond line'.encode()
        self.publish('text/plain;charset=utf-8', text)
        self.assertEqual(self.query('UTF8_STRING'), text)
        self.assertEqual(self.query('text/plain;charset=utf-8'), text)
        self.assertIn(b'TIMESTAMP', self.query('TARGETS').splitlines())

    def test_files_offer_uri_and_gnome_targets_with_incremental_transfer(self):
        # Exceed one X11 request and preserve names, directories and copy semantics.
        data = b''.join(f'file:///tmp/folder%20%D0%A2%D0%B5%D1%81%D1%82/archive-{i}.zip\r\n'.encode()
                        for i in range(2400))
        self.publish('text/uri-list', data)
        targets = self.query('TARGETS').splitlines()
        self.assertIn(b'text/uri-list', targets)
        self.assertIn(b'x-special/gnome-copied-files', targets)
        self.assertNotIn(b'UTF8_STRING', targets)
        for _ in range(2):
            self.assertEqual(self.query('text/uri-list'), data)
            self.assertEqual(self.query('x-special/gnome-copied-files'), b'copy\n' + b'\n'.join(data.splitlines()))
        self.assertGreater(self.timestamp(), 0)

    def test_primary_timestamp_reader_uses_integer_metadata(self):
        script = ('import runpy,sys; module=runpy.run_path(sys.argv[1]); '
                  'owner=module["ClipboardOwner"]("text/plain;charset=utf-8",b"old selection","PRIMARY"); '
                  'print(owner.timestamp,flush=True); owner.run()')
        primary = subprocess.Popen([sys.executable, '-c', script, str(OWNER)], env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.owners.append(primary)
        self.assertTrue(select.select([primary.stdout], [], [], 5)[0])
        stamp = int(primary.stdout.readline())
        self.publish(PNG, b'\x89PNG\r\n\x1a\nnew')
        value = subprocess.check_output([sys.executable, str(OWNER), '--primary-timestamp'], env=self.env, timeout=3)
        self.assertEqual(int(value), stamp)
        self.assertGreater(self.timestamp(), stamp)

    def test_multiple_target_request(self):
        from Xlib import X, Xatom, display
        data = b'\x89PNG\r\n\x1a\nsmall'
        self.publish(PNG, data)
        connection = display.Display(self.env['DISPLAY'])
        try:
            root = connection.screen().root
            window = root.create_window(0, 0, 1, 1, 0, X.CopyFromParent, X.InputOnly)
            atom = connection.intern_atom
            pairs = [atom(PNG), atom('IMAGE_OUT'), atom('TIMESTAMP'), atom('TIME_OUT'), atom('unsupported'), atom('NO_OUT')]
            window.change_property(atom('PAIRS'), atom('ATOM_PAIR'), 32, pairs)
            window.convert_selection(atom('CLIPBOARD'), atom('MULTIPLE'), atom('PAIRS'), X.CurrentTime)
            connection.flush()
            self.assertTrue(select.select([connection], [], [], 5)[0])
            reply = connection.next_event()
            self.assertEqual(reply.type, X.SelectionNotify)
            self.assertEqual(reply.property, atom('PAIRS'))
            self.assertEqual(bytes(window.get_full_property(atom('IMAGE_OUT'), X.AnyPropertyType).value), data)
            stamp = window.get_full_property(atom('TIME_OUT'), Xatom.INTEGER)
            self.assertEqual(stamp.format, 32)
            self.assertGreater(stamp.value[0], 0)
            self.assertEqual(window.get_full_property(atom('PAIRS'), atom('ATOM_PAIR')).value[-1], X.NONE)
        finally:
            connection.close()

    def test_many_incremental_requests_are_bounded_and_rejected_in_multiple_reply(self):
        from Xlib import X, display
        self.publish(PNG,b'\x89PNG\r\n\x1a\n'+b'x'*100000)
        connection=display.Display(self.env['DISPLAY'])
        try:
            window=connection.screen().root.create_window(0,0,1,1,0,X.CopyFromParent,X.InputOnly)
            atom=connection.intern_atom
            pairs=[value for index in range(40) for value in (atom(PNG),atom('BOUNDED_'+str(index)))]
            window.change_property(atom('PAIRS'),atom('ATOM_PAIR'),32,pairs)
            window.convert_selection(atom('CLIPBOARD'),atom('MULTIPLE'),atom('PAIRS'),X.CurrentTime)
            connection.flush()
            self.assertTrue(select.select([connection],[],[],5)[0])
            reply=connection.next_event();self.assertEqual(reply.property,atom('PAIRS'))
            result=window.get_full_property(atom('PAIRS'),atom('ATOM_PAIR')).value
            self.assertEqual(sum(bool(value) for value in result[1::2]),32)
            self.assertTrue(all(value==0 for value in result[65::2]))
        finally:connection.close()

    def test_oversized_multiple_property_is_rejected_without_partial_conversion(self):
        from Xlib import X, display
        self.publish(PNG,b'\x89PNG\r\n\x1a\nsmall')
        connection=display.Display(self.env['DISPLAY'])
        try:
            window=connection.screen().root.create_window(0,0,1,1,0,X.CopyFromParent,X.InputOnly)
            atom=connection.intern_atom
            pairs=[atom(PNG),atom('IMAGE_OUT')]*1025
            window.change_property(atom('PAIRS'),atom('ATOM_PAIR'),32,pairs)
            window.convert_selection(atom('CLIPBOARD'),atom('MULTIPLE'),atom('PAIRS'),X.CurrentTime)
            connection.flush()
            self.assertTrue(select.select([connection],[],[],5)[0])
            self.assertEqual(connection.next_event().property,X.NONE)
            self.assertIsNone(window.get_full_property(atom('IMAGE_OUT'),X.AnyPropertyType))
        finally:connection.close()

    @unittest.skipUnless(shutil.which('labwc') and shutil.which('wl-copy') and shutil.which('wl-paste'), 'Needs headless labwc and wl-clipboard')
    def test_complete_bridge_recopies_identical_images_after_primary_changes(self):
        with tempfile.TemporaryDirectory(prefix='karyo-clipboard-integration-') as temporary:
            root = Path(temporary)
            runtime = root / 'runtime'; runtime.mkdir(mode=0o700)
            config = root / 'labwc'; config.mkdir()
            (config / 'rc.xml').write_text('<labwc_config/>')
            (config / 'autostart').write_text('#!/bin/sh\nexit 0\n')
            env = {**self.env, 'XDG_RUNTIME_DIR': str(runtime), 'XDG_CONFIG_HOME': str(root / 'config'),
                   'WLR_BACKENDS': 'headless', 'WLR_RENDERER': 'pixman', 'WLR_HEADLESS_OUTPUTS': '1'}
            env.pop('WAYLAND_DISPLAY', None)
            server = subprocess.Popen(['labwc', '-C', str(config)], env=env, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL, start_new_session=True)
            bridge_process = None
            primary = None
            def eventually(check, label):
                deadline = time.monotonic() + 7
                while time.monotonic() < deadline:
                    try:
                        value = check()
                        if value: return value
                    except (subprocess.SubprocessError, ValueError): pass
                    time.sleep(.05)
                self.fail(label)
            def read_wayland():
                return subprocess.check_output(['wl-paste', '-n', '-t', PNG], env=env, stderr=subprocess.DEVNULL, timeout=1)
            def copy_png(data):
                subprocess.run(['wl-copy', '--type', PNG], env=env, input=data, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, check=True, timeout=3)
            def set_primary():
                script = ('import runpy,sys; module=runpy.run_path(sys.argv[1]); '
                          'owner=module["ClipboardOwner"]("text/plain;charset=utf-8",b"selection","PRIMARY"); '
                          'print(owner.timestamp,flush=True); owner.run()')
                worker = subprocess.Popen([sys.executable, '-c', script, str(OWNER)], env=env,
                                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                self.owners.append(worker)
                self.assertTrue(select.select([worker.stdout], [], [], 5)[0])
                return worker, int(worker.stdout.readline())
            try:
                socket = eventually(lambda: next((p for p in runtime.glob('wayland-*') if stat.S_ISSOCK(p.stat().st_mode)), None), 'Headless Wayland did not start')
                env['WAYLAND_DISPLAY'] = socket.name
                bridge_process = subprocess.Popen([sys.executable, str(OWNER.with_name('bridge.py'))],
                    env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
                time.sleep(.2)
                data = b'\x89PNG\r\n\x1a\ntransport fixture'
                for _ in range(3):
                    old_primary = primary
                    primary, stamp = set_primary()
                    if old_primary: old_primary.wait(timeout=3)
                    copy_png(data)
                    eventually(lambda: self.query(PNG) == data, 'Guest PNG was not bridged')
                    eventually(lambda: self.timestamp() > stamp, 'Identical re-copy lost to newer PRIMARY')
                    self.assertEqual(read_wayland(), data)
                    self.assertIsNone(bridge_process.poll())
                # A supported old X11 value must not clobber a marked local secret.
                hint=b'x-kde-passwordManagerHint'
                subprocess.run(['wl-copy','--type',hint.decode()],env=env,input=b'local sensitive marker',
                               check=True,timeout=3,stdout=subprocess.DEVNULL)
                time.sleep(.3)
                self.publish(PNG,b'\x89PNG\r\n\x1a\nremote while sensitive')
                time.sleep(.3)
                self.assertEqual(subprocess.check_output(['wl-paste','-n','-t',hint.decode()],env=env,timeout=2),
                                 b'local sensitive marker')
                copy_png(data)
                eventually(lambda:self.query(PNG)==data,'Ordinary sharing did not resume after sensitive copy')
                # An actual host/X11 clipboard owner must still reach Wayland.
                other = b'\x89PNG\r\n\x1a\nfrom X11'
                self.publish(PNG, other)
                eventually(lambda: read_wayland() == other, 'X11 clipboard event stayed buffered')
                self.assertIsNone(bridge_process.poll())
                files = b'file:///tmp/Archive%20%D0%A2%D0%B5%D1%81%D1%82.zip\r\nfile:///tmp/folder\r\n'
                subprocess.run(['wl-copy', '--type', 'text/uri-list'], env=env, input=files,
                               check=True, timeout=3, stdout=subprocess.DEVNULL)
                eventually(lambda: self.query('text/uri-list') == files, 'Wayland files did not reach X11')
                self.assertEqual(self.query('x-special/gnome-copied-files'), b'copy\n' + b'\n'.join(files.splitlines()))
                incoming = b'file:///run/vmblock-fuse/blockdir/123/Host%20archive.zip\r\n'
                self.publish('text/uri-list', incoming)
                def read_files():
                    return subprocess.check_output(['wl-paste', '-n', '-t', 'text/uri-list'], env=env,
                                                   stderr=subprocess.DEVNULL, timeout=1)
                eventually(lambda: read_files() == incoming, 'X11 files did not reach Wayland')
                self.assertIsNone(bridge_process.poll())
            finally:
                if primary and primary.poll() is None:
                    primary.terminate(); primary.wait(timeout=3)
                for process in (bridge_process, server):
                    if process and process.poll() is None:
                        os.killpg(process.pid, 15)
                        process.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
