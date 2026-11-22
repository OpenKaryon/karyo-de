#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real pointer/pixel checks of packaged compositor caption assets in private QA."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import tempfile
import time

from PIL import Image
from Xlib import X, Xatom, Xutil, display
from Xlib.protocol import event


def wait_for(test, message):
    deadline = time.monotonic() + 4
    while not test():
        if time.monotonic() > deadline:
            raise RuntimeError(message)
        time.sleep(.04)


def checks(env, output, baseline):
    connection = display.Display(env['DISPLAY'])
    root = connection.screen().root
    atom = connection.intern_atom
    w = root.create_window(100, 180, 440, 240, 0, connection.screen().root_depth,
        X.InputOutput, X.CopyFromParent, background_pixel=0x112233)
    w.set_wm_name('Short')
    w.set_wm_class('fixture.EXE', 'fixture.EXE')
    w.set_wm_protocols([atom('WM_DELETE_WINDOW')])
    w.change_property(atom('_NET_WM_PID'), Xatom.CARDINAL, 32, [os.getpid()])
    w.change_property(atom('_NET_WM_WINDOW_TYPE'), Xatom.ATOM, 32,
        [atom('_NET_WM_WINDOW_TYPE_NORMAL')])
    w.map()
    connection.sync()
    wait_for(lambda: w.get_attributes().map_state == X.IsViewable, 'Client did not map')
    time.sleep(.25)
    pointer = subprocess.Popen(['/usr/share/karyo-qa/pointer-probe'], env=env,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    observations = []

    def response(expected):
        with selectors.DefaultSelector() as ready:
            ready.register(pointer.stdout, selectors.EVENT_READ)
            assert ready.select(4), 'Private pointer stalled'
            assert pointer.stdout.readline().strip() == expected

    def command(text):
        pointer.stdin.write(text + '\n')
        pointer.stdin.flush()
        response('OK')
        connection.sync()
        time.sleep(.12)

    def move(x, y):
        size = root.get_geometry()
        command(f'move {x} {y} {size.width} {size.height}')

    def click():
        command('button 1')
        command('button 0')

    def activate():
        root.send_event(event.ClientMessage(window=w, client_type=atom('_NET_ACTIVE_WINDOW'),
            data=(32, [2, X.CurrentTime, 0, 0, 0])),
            event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
        connection.sync()
        time.sleep(.2)

    def capture(label):
        path = output / (label + '.png')
        subprocess.run(['grim', str(path)], env=env, check=True, timeout=5)
        with Image.open(path) as image:
            rgb = image.convert('RGB')
        observations.append({'stage':label, 'frame_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        return rgb

    def close_colors(image):
        g = w.get_geometry()
        pixels = [image.getpixel((g.x + g.width - 46 + x, g.y - 23 + y))
            for y in range(23) for x in range(46)]
        red = sum(p == (196, 43, 28) for p in pixels)
        # Thin diagonal white strokes are antialiased over red. Inspect only
        # their center, excluding the software cursor at the left hitbox edge.
        ink = [image.getpixel((g.x + g.width - 46 + x, g.y - 23 + y))
            for y in range(6, 18) for x in range(17, 30)]
        white = sum(p[0] >= p[1] >= p[2] and p[0] >= 225 and p[2] >= 170 for p in ink)
        return red, white

    def close_hover(label):
        g = w.get_geometry()
        move(g.x + g.width - 45, g.y - 12)
        image = capture(label)
        red, white = close_colors(image)
        assert red > 850 and white >= 6, (label, red, white)
        observations[-1].update({'red':red, 'white':white, 'width':46, 'height':23})

    try:
        response('READY')
        activate()
        move(20, 20)
        assert close_colors(capture('normal'))[0] == 0
        if baseline:
            g = w.get_geometry()
            move(g.x + g.width - 45, g.y - 12)
            assert close_colors(capture('old-close-hitbox'))[0] == 0
            return observations
        close_hover('close-hover')
        g = w.get_geometry()
        # Left edge of the new maximize hitbox lay in the old minimize/title area.
        move(g.x + g.width - 91, g.y - 12)
        assert close_colors(capture('max-hover'))[0] == 0
        click()
        wait_for(lambda: w.get_geometry().width == root.get_geometry().width, 'Maximize edge missed')
        close_hover('maximized-close-hover')
        g = w.get_geometry()
        move(g.x + g.width - 91, g.y - 12)
        capture('restore-hover')
        click()
        wait_for(lambda: w.get_geometry().width == 440, 'Restore edge missed')
        g = w.get_geometry()
        move(g.x + g.width - 137, g.y - 12)
        capture('minimize-hover')
        click()
        wait_for(lambda: w.get_wm_state().state == Xutil.IconicState, 'Minimize edge missed')
        capture('minimized')
        activate()
        wait_for(lambda: w.get_wm_state().state == Xutil.NormalState, 'Restore activation failed')
        close_hover('restored-close-hover')
        for width in (162, 161, 116, 104, 440):
            w.configure(width=width)
            connection.sync()
            time.sleep(.2)
            close_hover('width-' + str(width))
        # Close must deliver the normal application protocol, not kill the client.
        g = w.get_geometry()
        move(g.x + g.width - 45, g.y - 12)
        click()
        def deleted():
            while connection.pending_events():
                item = connection.next_event()
                if item.type == X.ClientMessage and item.client_type == atom('WM_PROTOCOLS'):
                    if item.window.id == w.id and item.data[1][0] == atom('WM_DELETE_WINDOW'):
                        return True
            return False
        wait_for(deleted, 'Close edge did not deliver WM_DELETE_WINDOW')
        return observations
    finally:
        if pointer.poll() is None:
            pointer.stdin.write('quit\n')
            pointer.stdin.flush()
            try:
                pointer.wait(timeout=3)
            except subprocess.TimeoutExpired:
                pointer.kill()
                pointer.wait(timeout=3)
        w.destroy()
        connection.sync()
        connection.close()


def main(checker=checks, description=__doc__, actor_path=Path(__file__), config_xml=None,
        private_pointer=True, real_wine_application=False):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', action='store_true', help='Require the earlier missing Windows buttons; not positive acceptance')
    args = parser.parse_args()
    assert os.getuid() == 1001 and os.environ.get('DBUS_SESSION_BUS_ADDRESS', '').startswith('unix:')
    args.output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='karyo-caption-buttons-') as temporary:
        private = Path(temporary)
        runtime = private / 'runtime'
        runtime.mkdir(mode=0o700)
        config = private / 'config'
        config.mkdir()
        ready = private / 'display'
        (config / 'rc.xml').write_text(config_xml or '<labwc_config><focus><followMouse>no</followMouse></focus></labwc_config>')
        (config / 'autostart').write_text('#!/bin/sh\nprintf "%s\\n" "$DISPLAY" > "' + str(ready) + '"\n')
        (config / 'autostart').chmod(0o700)
        env = {**os.environ, 'XDG_RUNTIME_DIR':str(runtime), 'XDG_CONFIG_HOME':str(private / 'xdg'),
            'HOME':str(private), 'XDG_DATA_HOME':str(private / 'data'),
            'WLR_BACKENDS':'headless', 'WLR_RENDERER':'pixman', 'WLR_HEADLESS_OUTPUTS':'1'}
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'LABWC_PID', 'XAUTHORITY'):
            env.pop(key, None)
        with (args.output / 'compositor.log').open('wb') as log:
            server = subprocess.Popen(['/usr/bin/labwc', '-C', str(config)], env=env,
                stdout=log, stderr=log, start_new_session=True)
            try:
                wait_for(lambda: ready.exists() or server.poll() is not None, 'Private compositor did not start')
                assert server.poll() is None
                env['DISPLAY'] = ready.read_text().strip()
                env['WAYLAND_DISPLAY'] = next(p.name for p in runtime.glob('wayland-*') if p.is_socket())
                env['KARYO_TEST_SERVER_PID'] = str(server.pid)
                frames = checker(env, args.output, args.baseline)
                report = {'accepted':True, 'baseline':args.baseline, 'uid':os.getuid(), 'real_wine_application':real_wine_application and not args.baseline,
                    'private_pointer':private_pointer, 'frames':frames,
                    'labwc_version':subprocess.check_output(['dpkg-query', '-W', '-f=${Version}', 'labwc'], text=True),
                    'actor_sha256':hashlib.sha256(actor_path.read_bytes()).hexdigest()}
                (args.output / 'acceptance.json').write_text(json.dumps(report, indent=2) + '\n')
                print(json.dumps(report), flush=True)
            finally:
                if server.poll() is None:
                    os.killpg(server.pid, signal.SIGTERM)
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(server.pid, signal.SIGKILL)
                        server.wait(timeout=3)


if __name__ == '__main__':
    main()
