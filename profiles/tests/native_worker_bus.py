#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Own-bus IPC/lifecycle experiment, not an installed desktop worker broker."""
import argparse
from contextlib import ExitStack
import fcntl
from dataclasses import asdict
import json
import os
from pathlib import Path
import socket
import signal
import struct
import subprocess
import sys
import tempfile
import time
import uuid

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

NAME = 'org.karyo.DesktopWorkers1'
OBJECT = '/org/karyo/DesktopWorkers1'


def bus_id(bus):
    daemon = dbus.Interface(bus.get_object('org.freedesktop.DBus',
        '/org/freedesktop/DBus'), 'org.freedesktop.DBus')
    return str(daemon.GetId())


def started(pid):
    return int(Path('/proc', str(pid), 'stat').read_text().rpartition(') ')[2].split()[19])


def identity(pid):
    root = Path('/proc', str(pid))
    def node(path):
        value = path.stat()
        return [value.st_dev, value.st_ino]
    return {'pid': pid, 'start': started(pid), 'net': node(root / 'ns/net'),
            'mount': node(root / 'ns/mnt'), 'cgroup': (root / 'cgroup').read_text().strip()}


class Cancelled(dbus.DBusException):
    _dbus_error_name = NAME + '.Error.Cancelled'


class Fixture(dbus.service.Object):
    def __init__(self, bus, folder, registry=None, managed_entry=False):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from karyo_profiles.worker_origin import WorkerOrigin
        self.capture = WorkerOrigin
        self.bus, self.folder = bus, folder
        self.requests = {}
        self.registry = registry
        self.managed_entry = managed_entry
        self.name = dbus.service.BusName(NAME, bus, do_not_queue=True)
        super().__init__(self.name, OBJECT)
        bus.add_signal_receiver(self.owner_changed, signal_name='NameOwnerChanged',
                                dbus_interface='org.freedesktop.DBus')

    @dbus.service.method(NAME, in_signature='ss', out_signature='h', sender_keyword='sender',
                         async_callbacks=('reply', 'error'))
    def Activate(self, declaration, request, reply, error, sender=None):
        if str(declaration) != 'org.karyo.Fixture' or len(request) != 32:
            raise ValueError('This experiment has one fixed fixture declaration')
        key = (str(sender), str(request))
        if key in self.requests:
            raise ValueError('Request already used')
        origin = self.capture(self.bus, str(sender))
        try:
            caller = asdict(origin.verify())
            context = None
            if self.registry is not None:
                from karyo_profiles.worker_context import WorkerContext
                with self.registry.store.locked():
                    context = WorkerContext.capture(origin, self.registry)
            placement = None
            if self.managed_entry:
                sys.path.insert(0, str(Path(__file__).resolve().parent))
                from native_worker_unit import probe
                placement = probe(context)
            endpoint = self.folder / ('endpoint-' + str(request))
            process = subprocess.Popen([sys.executable, '-B', __file__, '--worker',
                str(endpoint), str(caller['pid']), str(caller['uid']), str(caller['start'])])
        except BaseException:
            origin.close()
            raise
        self.requests[key] = {'origin': origin, 'context': context, 'placement': placement, 'caller': caller, 'process': process, 'endpoint': endpoint,
                              'reply': reply, 'error': error, 'pending': True, 'cancelled': False, 'deadline': time.monotonic() + 5}
        GLib.timeout_add(400, self.ready, key)

    def ready(self, key):
        request = self.requests[key]
        if request['cancelled']:
            return False
        process = request['process']
        try:
            current = asdict(request['origin'].verify())
            if request['context'] is not None:
                with self.registry.store.locked():
                    request['context'].verify()
        except Exception:
            self.cancel(key)
            return False
        if current != request['caller'] or process.poll() is not None:
            self.cancel(key)
            return False
        if not request['endpoint'].exists():
            if time.monotonic() >= request['deadline']:
                self.cancel(key)
                return False
            return True
        node = request['endpoint'].stat()
        value = {'version': 1, 'role': 'fixture-activation', 'request': key[1],
                 'bus': bus_id(self.bus), 'owner': str(self.bus.get_unique_name()),
                 'gui': request['caller'], 'worker': identity(process.pid),
                 'endpoint': str(request['endpoint']), 'socket': [node.st_dev, node.st_ino],
                 'desktop': None if request['context'] is None else request['context'].desktop_token,
                 'placement': request['placement']}
        fd = os.memfd_create('karyo-fixture-activation', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
        try:
            os.write(fd, json.dumps(value).encode())
            fcntl.fcntl(fd, fcntl.F_ADD_SEALS, fcntl.F_SEAL_WRITE | fcntl.F_SEAL_GROW |
                        fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_SEAL)
            request['pending'] = False
            request['reply'](dbus.types.UnixFd(fd))
        finally:
            os.close(fd)
        return False

    def cancel(self, key):
        request = self.requests[key]
        if request['cancelled']:
            return
        request['cancelled'] = True
        process = request['process']
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)
        request['endpoint'].unlink(missing_ok=True)
        request['origin'].close()
        if request['pending']:
            request['pending'] = False
            request['error'](Cancelled('Fixture activation was cancelled'))

    @dbus.service.method(NAME, in_signature='s', out_signature='', sender_keyword='sender')
    def Cancel(self, request, sender=None):
        self.cancel((str(sender), str(request)))

    @dbus.service.method(NAME, in_signature='s', out_signature='', sender_keyword='sender')
    def Release(self, request, sender=None):
        self.cancel((str(sender), str(request)))

    def owner_changed(self, name, old, new):
        if str(old) and not str(new):
            for key in list(self.requests):
                if key[0] == str(name):
                    self.cancel(key)


def worker(endpoint, pid, uid, start):
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind(endpoint)
    os.chmod(endpoint, 0o666)  # Own disposable experiment, not a production endpoint.
    listener.listen(1)
    connection, _ = listener.accept()
    actual = struct.unpack('3i', connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    if actual[:2] != (pid, uid) or started(pid) != start:
        raise RuntimeError('Direct GUI peer differs from the activation origin')
    connection.send(json.dumps({'pid': actual[0], 'uid': actual[1], 'start': started(pid)}).encode())
    while connection.recv(128):
        pass
    connection.close()
    listener.close()


def client(address, cancel, managed_entry=False):
    bus = dbus.bus.BusConnection(address)
    remote = dbus.Interface(bus.get_object(NAME, OBJECT), NAME)
    loop = GLib.MainLoop()
    request = uuid.uuid4().hex
    result = {}

    def accepted(unix_fd):
        fd = unix_fd.take()
        try:
            os.lseek(fd, 0, os.SEEK_SET)
            record = json.loads(os.read(fd, 65536))
            seals = fcntl.fcntl(fd, fcntl.F_GET_SEALS)
            assert seals & (fcntl.F_SEAL_WRITE | fcntl.F_SEAL_GROW | fcntl.F_SEAL_SHRINK) == 14
        finally:
            os.close(fd)
        assert not cancel
        assert record['gui']['pid'] == os.getpid() and record['gui']['uid'] == os.getuid()
        assert record['gui']['start'] == started(os.getpid())
        assert record['bus'] == bus_id(bus) and record['owner'] == str(bus.get_name_owner(NAME))
        channel = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        channel.settimeout(3)
        channel.connect(record['endpoint'])
        peer = struct.unpack('3i', channel.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        assert peer[0] == record['worker']['pid'] and peer[1] == 0
        actual = json.loads(channel.recv(1024))
        assert actual['pid'] == os.getpid() and actual['uid'] == os.getuid()
        result.update(direct_gui_peer=True, sealed_fd=True, root_worker_peer=True, desktop_context_bound=record['desktop'] is not None, managed_placement=record['placement'])
        channel.close()
        remote.Release(request)
        loop.quit()

    def rejected(error):
        assert cancel and error.get_dbus_name() == NAME + '.Error.Cancelled', str(error)
        result['cancelled_before_late_ready'] = True
        loop.quit()

    remote.Activate('org.karyo.Fixture', request, reply_handler=accepted, error_handler=rejected)
    if cancel:
        GLib.timeout_add(150, lambda: (remote.Cancel(request), False)[1])
    GLib.timeout_add(30000 if managed_entry else 5000, lambda: (loop.quit(), False)[1])
    loop.run()
    assert result, 'Own IPC experiment timed out'
    bus.close()
    print(json.dumps(result))


def experiment(user, desktop_context, managed_entry):
    if desktop_context:
        assert socket.gethostname() in ('karyo-network-debian', 'karyo-network-ubuntu')
    assert os.getuid() == 0 and sys.platform == 'linux'
    with tempfile.TemporaryDirectory(prefix='karyo-worker-bus-') as temporary:
        folder = Path(temporary)
        folder.chmod(0o755)
        address = 'unix:path=' + str(folder / 'bus')
        config = folder / 'bus.conf'
        config.write_text('<busconfig><type>system</type><listen>' + address + '</listen>'
            '<policy user="root"><allow own="*"/><allow send_destination="*"/>'
            '<allow receive_sender="*"/></policy><policy context="default">'
            '<allow user="*"/><allow send_destination="org.freedesktop.DBus"/>'
            '<allow send_destination="org.karyo.DesktopWorkers1"/>'
            '<allow receive_sender="*"/></policy></busconfig>')
        daemon = subprocess.Popen(['/usr/bin/dbus-daemon', '--nofork', '--config-file=' + str(config)])
        service = None
        try:
            deadline = time.monotonic() + 5
            while not (folder / 'bus').exists():
                assert daemon.poll() is None and time.monotonic() < deadline
                time.sleep(.02)
            service = subprocess.Popen([sys.executable, '-B', __file__, '--service', address, str(folder)]
                + (['--desktop-context'] if desktop_context else [])
                + (['--managed-entry'] if managed_entry else []))
            bus = dbus.bus.BusConnection(address)
            while not bus.name_has_owner(NAME):
                assert service.poll() is None and time.monotonic() < deadline
                time.sleep(.02)
            source = Path(__file__).read_text()
            observations = []
            for cancel in (False, True):
                command = ['/usr/sbin/runuser', '-u', user, '--', sys.executable, '-B', '-c',
                           source, '--client', address] + (['--cancel'] if cancel else []) + (['--managed-entry'] if managed_entry else [])
                if desktop_context:
                    import pwd
                    uid = pwd.getpwnam(user).pw_uid
                    command = command[:3] + ['--', '/usr/bin/env', 'XDG_RUNTIME_DIR=/run/user/'+str(uid),
                        'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/'+str(uid)+'/bus',
                        '/usr/bin/systemd-run', '--user', '--wait', '--pipe', '--collect', '--quiet',
                        '--slice=app.slice', '--unit=app-karyo-worker-probe-'+uuid.uuid4().hex+'.service',
                        '--', *command[4:]]
                result = subprocess.run(command, text=True, capture_output=True, timeout=35)
                assert result.returncode == 0, result.stdout + result.stderr
                observations.append(json.loads(result.stdout))
            time.sleep(.6)
            assert not list(folder.glob('endpoint-*')), 'Fixture endpoint survived release/cancel'
            print(json.dumps({'observations': observations, 'endpoints_retired': True,
                'scope': 'direct D-Bus, optional captured GUI context and listener/cancel lifecycle only',
                'desktop_context_bound': desktop_context,
                'worker_placement_fixture': managed_entry,
                'production_authorization_or_registered_broker': False}))
            bus.close()
        finally:
            if service is not None:
                service.terminate()
                service.wait(timeout=5)
            daemon.terminate()
            daemon.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--service', nargs=2)
    parser.add_argument('--worker', nargs=4)
    parser.add_argument('--client')
    parser.add_argument('--cancel', action='store_true')
    parser.add_argument('--user')
    parser.add_argument('--desktop-context', action='store_true')
    parser.add_argument('--managed-entry', action='store_true')
    args = parser.parse_args()
    DBusGMainLoop(set_as_default=True)
    if args.worker:
        worker(args.worker[0], *map(int, args.worker[1:]))
    elif args.service:
        bus = dbus.bus.BusConnection(args.service[0])
        with ExitStack() as stack:
            registry = None
            if args.desktop_context:
                sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
                from karyo_profiles.journal import RuntimeJournal
                from karyo_profiles.storage import ConfigStore
                from karyo_profiles.journal_session import SessionRegistry
                journal = stack.enter_context(RuntimeJournal())
                registry = SessionRegistry(journal, ConfigStore()).restore()
                stack.callback(registry.release_handles)
            fixture = Fixture(bus, Path(args.service[1]), registry, args.managed_entry)
            loop = GLib.MainLoop()
            signal.signal(signal.SIGTERM, lambda *unused: loop.quit())
            try:
                loop.run()
            finally:
                for key in fixture.requests:
                    fixture.cancel(key)
    elif args.client:
        client(args.client, args.cancel, args.managed_entry)
    else:
        if not args.user:
            parser.error("The experiment needs --user for its disposable ordinary account")
        if args.managed_entry and not args.desktop_context:
            parser.error('--managed-entry requires the isolated desktop-context fixture')
        experiment(args.user, args.desktop_context, args.managed_entry)


if __name__ == '__main__':
    main()
