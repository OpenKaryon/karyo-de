#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual Wine hidden-owner palette stacking and scene minimize/restore QA."""
import argparse, hashlib, json, os, pwd, signal, struct, subprocess, tempfile, time
from pathlib import Path
from Xlib import X, Xatom, display, error
from Xlib.protocol import event
from PIL import Image

def require(value, message):
    if not value:
        raise RuntimeError(message)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bits', type=int, choices=(32, 64), default=64)
    parser.add_argument('--compositor-package', type=Path)
    parser.add_argument('--trace-wayland', action='store_true', help='Capture Xwayland client protocol for a missing-surface investigation')
    parser.add_argument('--mapped-owner', action='store_true', help='Stress a visible 1x1 no-activate helper; not the canonical hidden owner')
    parser.add_argument('--baseline', action='store_true', help='Observe old behavior without asserting a missing group')
    args = parser.parse_args()
    require(os.getuid() == 1001 and pwd.getpwuid(os.getuid()).pw_name == 'win32qa', 'Use the disposable QA account')
    require(os.environ.get('DBUS_SESSION_BUS_ADDRESS', '').startswith('unix:'), 'Private session bus required')
    home = Path(pwd.getpwuid(os.getuid()).pw_dir)
    require(not (home / '.local').exists(), 'Use a fresh disposable home')
    args.output.mkdir(exist_ok=False, parents=True)
    program = Path('/usr/share/karyo-qa/palette-probe' + ('32' if args.bits == 32 else '') + '.exe')
    data = program.read_bytes()
    offset = struct.unpack_from('<I', data, 60)[0]
    require(struct.unpack_from('<H', data, offset + 4)[0] == (332 if args.bits == 32 else 34404), 'Wrong fixture bitness')
    report = {'pe_bits': args.bits, 'baseline': args.baseline, 'mapped_owner': args.mapped_owner, 'cases': [], 'actor_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'producer_sha256': hashlib.sha256(data).hexdigest()}
    with tempfile.TemporaryDirectory(prefix='karyo-wine-palettes-') as tmp:
        work = Path(tmp)
        runtime = work / 'runtime'
        runtime.mkdir(mode=448)
        config = work / 'config'
        config.mkdir()
        ready = work / 'display'
        (config / 'rc.xml').write_text('<labwc_config><focus><followMouse>no</followMouse></focus></labwc_config>')
        (config / 'autostart').write_text('#!/bin/sh\nprintf "%s\\n" "$DISPLAY" > "' + str(ready) + '"\n')
        (config / 'autostart').chmod(448)
        compositor = Path('/usr/bin/labwc')
        if args.compositor_package:
            manifest = json.loads((args.compositor_package / 'manifest.json').read_text())
            name = manifest['binaries']['labwc']
            record = next((row for row in manifest['artifacts'] if row['file'] == name))
            package = args.compositor_package / name
            require(package.name == name and (not package.is_symlink()) and (hashlib.sha256(package.read_bytes()).hexdigest() == record['sha256']), 'Changed compositor package')
            release = dict((line.split('=', 1) for line in Path('/etc/os-release').read_text().splitlines() if '=' in line))
            require(all((release[key].strip('"') == value for key, value in manifest['environment']['distribution'].items())), 'Wrong native compositor base')
            installed = work / 'installed'
            subprocess.run(['dpkg-deb', '-x', str(package), str(installed)], check=True)
            compositor = installed / 'usr/bin/labwc'
            report['compositor_version'] = manifest['version']
            report['compositor_sha256'] = record['sha256']
        else:
            report['compositor_version'] = subprocess.check_output(['dpkg-query', '-W', '-f=${Version}', 'labwc'], text=True)
        env = {**os.environ, 'LANG': 'C.UTF-8', 'LC_ALL': 'C.UTF-8', 'XDG_RUNTIME_DIR': str(runtime), 'XDG_CONFIG_HOME': str(work / 'xdg'), 'WLR_BACKENDS': 'headless', 'WLR_RENDERER': 'pixman', 'WLR_HEADLESS_OUTPUTS': '1', 'WINEDEBUG': '-all', 'WINEDLLOVERRIDES': 'mscoree,mshtml=d'}
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'LABWC_PID', 'XAUTHORITY'):
            env.pop(key, None)
        application = None
        connection = None
        with (args.output / 'compositor.log').open('wb') as server_log, (args.output / 'launcher.log').open('wb') as app_log:
            server_env = {**env, **({'WAYLAND_DEBUG':'client'} if args.trace_wayland else {})}
            server = subprocess.Popen([str(compositor), '-d', '-C', str(config)], env=server_env, stdout=server_log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 10
                while not ready.exists():
                    require(server.poll() is None and time.monotonic() < deadline, 'Private compositor failed')
                    time.sleep(0.05)
                env['DISPLAY'] = ready.read_text().strip()
                env['WAYLAND_DISPLAY'] = next((p.name for p in runtime.glob('wayland-*') if p.is_socket()))
                connection = display.Display(env['DISPLAY'])
                root = connection.screen().root
                atom = connection.intern_atom
                application = subprocess.Popen(['/usr/bin/karyo-win32', '--no-register', '--app-id', 'qa-palettes', str(program), *(['--mapped-owner'] if args.mapped_owner else [])], env=env, stdout=app_log, stderr=subprocess.STDOUT)
                windows = {}
                deadline = time.monotonic() + 150
                registered_deadline = None
                names = {'Karyo Palette Owner', 'Karyo Palette Main', 'Karyo Palette Foreign', *('Karyo Palette ' + str(i) for i in range(4))}
                while True:
                    require(application.poll() is None and time.monotonic() < deadline, 'Actual Wine windows did not appear')
                    for window in root.query_tree().children:
                        try:
                            name = window.get_wm_name()
                            if name in names:
                                windows[name] = window
                        except error.XError:
                            pass
                    clients = root.get_full_property(atom('_NET_CLIENT_LIST'), Xatom.WINDOW)
                    if set(windows) == names:
                        if registered_deadline is None:
                            registered_deadline = time.monotonic() + 10
                        expected = {w.id for name, w in windows.items() if args.mapped_owner or name != 'Karyo Palette Owner'}
                        report['published_clients'] = list(map(int, clients.value)) if clients is not None else []
                        report['missing_clients'] = list(expected - set(report['published_clients']))
                        if not report['missing_clients']:
                            break
                        require(time.monotonic() < registered_deadline, 'Real X11 clients never reached the compositor')
                    time.sleep(0.03)
                owner = windows['Karyo Palette Owner']
                frame = windows['Karyo Palette Main']
                foreign = windows['Karyo Palette Foreign']
                palettes = [windows['Karyo Palette ' + str(i)] for i in range(4)]
                require(owner.get_attributes().map_state == (X.IsViewable if args.mapped_owner else X.IsUnmapped), 'Wrong native helper visibility')
                geometry = owner.get_geometry()
                require(geometry.width <= 1 and geometry.height <= 1, 'Owner is not tiny')
                pids = []
                hints = []
                for window in [owner, frame, *palettes]:
                    prop = window.get_full_property(atom('_NET_WM_PID'), Xatom.CARDINAL)
                    pids.append(int(prop.value[0]) if prop is not None else 0)
                    require(program.name in (window.get_wm_class() or ()), 'Actual Wine class differs')
                require(pids[0] > 0 and len(set(pids)) == 1, 'Actual process hints differ')
                for window in [frame, *palettes]:
                    parent = window.get_wm_transient_for()
                    hints.append(parent.id if parent else None)
                    require(parent is not None and parent.id == owner.id, 'Actual shared ownership differs')
                report['window_hints'] = {name: {'type': list(map(int, window.get_full_property(atom('_NET_WM_WINDOW_TYPE'), Xatom.ATOM).value)), 'input': int(getattr(window.get_wm_hints(), 'input', -1)), 'override_redirect': bool(window.get_attributes().override_redirect), 'map_state': window.get_attributes().map_state, 'protocols': [connection.get_atom_name(a) for a in window.get_wm_protocols()], 'type_names': [connection.get_atom_name(a) for a in window.get_full_property(atom('_NET_WM_WINDOW_TYPE'), Xatom.ATOM).value], 'motif': list(map(int, window.get_full_property(atom('_MOTIF_WM_HINTS'), X.AnyPropertyType).value)) if window.get_full_property(atom('_MOTIF_WM_HINTS'), X.AnyPropertyType) else []} for name, window in windows.items()}
                report['geometries'] = {name: {key: getattr(window.get_geometry(), key) for key in ('x', 'y', 'width', 'height')} for name, window in windows.items()}
                report['identity'] = {'pid': pids[0], 'owner': owner.id, 'transient_hints': hints, 'class': program.name, 'tiny_owner': True, 'owner_mapped': args.mapped_owner}

                def activate(window):
                    root.send_event(event.ClientMessage(window=window, client_type=atom('_NET_ACTIVE_WINDOW'), data=(32, [2, 0, 0, 0, 0])), event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
                    connection.sync()
                    time.sleep(0.4)
                    active = root.get_full_property(atom('_NET_ACTIVE_WINDOW'), Xatom.WINDOW)
                    report.setdefault('activation', []).append({'requested': window.get_wm_name(), 'active': list(map(int, active.value)) if active else [], 'stack': [w.id for w in root.query_tree().children]})

                def shot(name, point, wanted=None):
                    path = args.output / (name + '.png')
                    deadline = time.monotonic() + 3
                    while True:
                        subprocess.run(['grim', str(path)], env=env, check=True, timeout=5)
                        with Image.open(path) as image:
                            color = image.convert('RGB').getpixel(point)
                        if wanted is None or color == wanted:
                            return color
                        require(time.monotonic() < deadline, f'{name}: observed {color}, expected {wanted}')
                        time.sleep(0.1)
                root.change_attributes(event_mask=X.PropertyChangeMask)
                connection.sync()

                def stable_focus(stage):
                    while connection.pending_events():
                        connection.next_event()
                    deadline = time.monotonic() + 3
                    quiet_since = time.monotonic()
                    changes = 0
                    while time.monotonic() - quiet_since < 0.4:
                        while connection.pending_events():
                            notification = connection.next_event()
                            if notification.type == X.PropertyNotify and notification.atom == atom('_NET_ACTIVE_WINDOW'):
                                quiet_since = time.monotonic()
                                changes += 1
                        require(time.monotonic() < deadline, 'Focus did not settle after ' + stage)
                        time.sleep(0.02)
                    report.setdefault('focus_quiet', []).append({'stage':stage, 'transitions_before_quiet':changes, 'quiet_seconds':0.4})

                g = frame.get_geometry()
                shot('main-painted', (g.x + 20, g.y + 20), (80, 80, 80))
                stable_focus('initial paint')
                points = []
                colors = [(22, 132, 64), (200, 68, 34), (45, 85, 175), (164, 122, 32)]
                for index, window in enumerate(palettes):
                    window.configure(x=g.x + 60 + index % 2 * 320, y=g.y + 100 + index // 2 * 220)
                    connection.sync()
                    time.sleep(0.12)
                    pos = window.get_geometry()
                    points.append((pos.x + pos.width // 2, pos.y + pos.height // 2))
                activate(palettes[0])
                shot('palette-visible', points[0], colors[0])
                activate(frame)
                first = shot('main-raised', points[0], None if args.baseline else colors[0])
                report['after_main_pixel'] = first
                require(first in (colors[0], (80, 80, 80)), 'Baseline window paint not established')
                report['baseline_missing_group'] = first != colors[0]
                if not args.baseline:
                    for i, window in enumerate(palettes):
                        activate(window)
                        activate(frame)
                        shot('palette-' + str(i), points[i], colors[i])
                    report['cases'].append('four real Wine palettes above main')
                    foreign.configure(x=0, y=0, width=1280, height=720)
                    connection.sync()
                    activate(foreign)
                    shot('foreign-occlusion', points[0], (156, 36, 156))
                    activate(frame)
                    for i, point in enumerate(points):
                        shot('returned-' + str(i), point, colors[i])
                    report['cases'].append('ordinary unrelated frame occlusion and return')
                    foreign.configure(x=1100, y=20, width=100, height=100)
                    connection.sync()
                    time.sleep(0.2)
                    root.send_event(event.ClientMessage(window=frame, client_type=atom('WM_CHANGE_STATE'), data=(32, [3, 0, 0, 0, 0])), event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
                    connection.sync()
                    time.sleep(0.2)
                    for i, window in enumerate([frame, *palettes]):
                        require(window.get_wm_state().state == 1, 'Wine handle entered IconicState')
                    for i, point in enumerate(points):
                        shot('minimized-' + str(i), point, (0, 0, 0))
                    require(application.poll() is None, 'Wine application exited on minimize')
                    activate(frame)
                    for i, window in enumerate(palettes):
                        shot('restored-' + str(i), points[i], colors[i])
                        require(window.get_wm_transient_for().id == owner.id, 'Ownership was rewritten')
                    stable_focus('restored group')
                    report['cases'].append('group hide/restore preserves real NormalState and process')
                if not args.baseline:
                    outsider = root.create_window(1040, 560, 160, 100, 0, connection.screen().root_depth, X.InputOutput, X.CopyFromParent, background_pixel=0x224466)
                    outsider.set_wm_name('Karyo foreign-process focus QA')
                    outsider.set_wm_class('karyo-foreign-focus-qa', 'karyo-foreign-focus-qa')
                    outsider.set_wm_hints(flags=1, input=1)
                    outsider.change_property(atom('_NET_WM_PID'), Xatom.CARDINAL, 32, [os.getpid()])
                    outsider.change_property(atom('_NET_WM_WINDOW_TYPE'), Xatom.ATOM, 32, [atom('_NET_WM_WINDOW_TYPE_NORMAL')])
                    outsider.map()
                    connection.sync()
                    deadline = time.monotonic() + 5
                    while True:
                        clients = root.get_full_property(atom('_NET_CLIENT_LIST'), Xatom.WINDOW)
                        if clients is not None and outsider.id in clients.value:
                            break
                        require(time.monotonic() < deadline, 'Foreign-process window did not map')
                        time.sleep(0.03)
                    allowed = {window.id for window in [frame, *palettes]}

                    def focus_owner():
                        focus = connection.get_input_focus().focus
                        if not hasattr(focus, 'id'):
                            return int(focus)
                        for _ in range(20):
                            if focus.id in allowed or focus.id == root.id or focus.id == outsider.id:
                                return focus.id
                            focus = focus.query_tree().parent
                        raise RuntimeError('Unexpected X11 input ancestor depth')

                    activate(frame)
                    shot('focus-click-target', (g.x + 20, g.y + 20), (80, 80, 80))
                    extent = root.get_geometry()
                    commands = f'move {g.x + 20} {g.y + 20} {extent.width} {extent.height}\nbutton 1\nbutton 0\nquit\n'
                    pointer = subprocess.run(['/usr/share/karyo-qa/pointer-probe'], input=commands, text=True, capture_output=True, env=env, check=True, timeout=8)
                    require(pointer.stdout.splitlines() == ['READY', 'OK', 'OK', 'OK'], 'Private pointer did not acknowledge the real activation click')
                    deadline = time.monotonic() + 3
                    while focus_owner() not in allowed:
                        require(time.monotonic() < deadline, 'Wine did not accept input focus before the negative probe')
                        time.sleep(0.03)
                    stable_focus('real pointer activation')
                    report['foreign_focus_before'] = focus_owner()
                    outsider.set_input_focus(X.RevertToParent, X.CurrentTime)
                    connection.sync()
                    deadline = time.monotonic() + 3
                    while True:
                        report['foreign_focus_after'] = focus_owner()
                        if report['foreign_focus_after'] in allowed:
                            break
                        require(time.monotonic() < deadline, 'Unrelated process took Wine input focus')
                        time.sleep(0.03)
                    report['cases'].append('unsolicited cross-process X11 focus remains denied')
                    outsider.destroy()
                    connection.sync()
                    activate(frame)
                    stable_focus('foreign-process focus denial')
                frame.send_event(event.ClientMessage(window=frame, client_type=atom('WM_PROTOCOLS'), data=(32, [atom('WM_DELETE_WINDOW'), 0, 0, 0, 0])))
                connection.sync()
                require(application.wait(timeout=10) == 0, 'Own Win32 probe did not close normally')
                application = None
                report['accepted'] = True
            finally:
                report['compositor_process'] = Path('/proc/' + str(server.pid) + '/stat').read_text() if Path('/proc/' + str(server.pid) + '/stat').exists() else None
                logs = args.output / 'engine-logs'
                logs.mkdir(exist_ok=True)
                for item in (home / '.local/share/karyo/windows/logs').glob('*'):
                    if item.is_file() and (not item.is_symlink()):
                        with item.open('rb') as stream:
                            stream.seek(max(0, item.stat().st_size - 262144))
                            data = stream.read(262144)
                        (logs / item.name).write_bytes(data)
                (args.output / 'progress.json').write_text(json.dumps(report, indent=2) + '\n')
                if application is not None and application.poll() is None:
                    application.terminate()
                    application.wait(timeout=5)
                if connection is not None:
                    connection.close()
                if server.poll() is None:
                    os.killpg(server.pid, signal.SIGTERM)
                    try:
                        server.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        os.killpg(server.pid, signal.SIGKILL)
                        server.wait(timeout=3)
    (args.output / 'acceptance.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
if __name__ == '__main__':
    main()
