#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render and actually click generic Wine corner masks in a private display."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import time

from PIL import Image
from Xlib import X, Xatom, Xutil, display
from Xlib.protocol import event


BACKGROUND = (30, 100, 50)
CLIENT = (17, 34, 51)


def checks(env, output, baseline):
    connection = display.Display(env['DISPLAY'])
    root = connection.screen().root
    atom = connection.intern_atom
    created = []
    observations = []
    size = root.get_geometry()
    pointer = subprocess.Popen(['/usr/share/karyo-qa/pointer-probe'], env=env,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def response(expected):
        with selectors.DefaultSelector() as ready:
            ready.register(pointer.stdout, selectors.EVENT_READ)
            assert ready.select(4), 'Private pointer stalled'
            assert pointer.stdout.readline().strip() == expected

    def command(line):
        pointer.stdin.write(line + '\n')
        pointer.stdin.flush()
        response('OK')
        connection.sync()
        time.sleep(.1)

    def move(x, y):
        command(f'move {x} {y} {size.width} {size.height}')

    def activate(w):
        root.send_event(event.ClientMessage(window=w, client_type=atom('_NET_ACTIVE_WINDOW'),
            data=(32, [2, X.CurrentTime, 0, 0, 0])),
            event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
        connection.sync()
        time.sleep(.2)

    def window(name, x, y, width, height, *, frame=False, kind='fixture.EXE',
            parent=None, window_type='NORMAL', color=0x112233, alpha=False, override=False, mapped=True):
        extra = {'override_redirect':int(override), 'background_pixel':color,
            'event_mask':X.ButtonPressMask | X.StructureNotifyMask}
        depth, visual = connection.screen().root_depth, X.CopyFromParent
        if alpha:
            layer = next(d for d in connection.screen().allowed_depths if d.depth == 32)
            visual = next(v.visual_id for v in layer.visuals if v.visual_class == X.TrueColor)
            colormap = root.create_colormap(visual, X.AllocNone)
            extra.update(colormap=colormap, border_pixel=0)
            depth = 32
        w = root.create_window(x, y, width, height, 0, depth, X.InputOutput, visual, **extra)
        created.append(w)
        w.set_wm_name(name)
        w.set_wm_class(kind, kind)
        w.set_wm_protocols([atom('WM_DELETE_WINDOW')])
        w.change_property(atom('_NET_WM_PID'), Xatom.CARDINAL, 32, [os.getpid()])
        w.change_property(atom('_NET_WM_WINDOW_TYPE'), Xatom.ATOM, 32,
            [atom('_NET_WM_WINDOW_TYPE_' + window_type)])
        w.change_property(atom('_MOTIF_WM_HINTS'), atom('_MOTIF_WM_HINTS'), 32,
            [3, 62 if frame else 36, 126 if frame else 0, 0, 0])
        if parent:
            w.set_wm_transient_for(parent)
            w.set_wm_hints(flags=1 | 64, input=1, window_group=parent)
        if mapped:
            w.map()
            connection.sync()
            time.sleep(.2)
            w.configure(x=x, y=y, width=width, height=height)
        connection.sync()
        time.sleep(.2)
        return w

    def capture(label):
        move(size.width - 20, size.height - 30)
        p = output / (label + '.png')
        subprocess.run(['grim', str(p)], env=env, check=True, timeout=5)
        with Image.open(p) as image:
            rgb = image.convert('RGB')
        observations.append({'stage':label, 'frame_sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
        return rgb

    def corners(w, label, *, framed=False, rounded=True):
        activate(w)
        image = capture(label)
        g = w.get_geometry()
        if framed:
            points = [(g.x - 1, g.y - 24), (g.x + g.width, g.y - 24),
                (g.x - 1, g.y + g.height), (g.x + g.width, g.y + g.height)]
        else:
            points = [(g.x, g.y), (g.x + g.width - 1, g.y),
                (g.x, g.y + g.height - 1), (g.x + g.width - 1, g.y + g.height - 1)]
        pixels = [image.getpixel(p) for p in points]
        if rounded:
            assert all(p == BACKGROUND for p in pixels), (label, points, pixels)
        elif not framed:
            assert all(p == CLIENT for p in pixels), (label, points, pixels)
        else:
            assert all(p != BACKGROUND for p in pixels), (label, points, pixels)
        assert image.getpixel((g.x + 15, g.y + 15)) == CLIENT
        observations[-1].update({'rounded':rounded, 'corners':pixels,
            'geometry':[g.x, g.y, g.width, g.height]})

    def click_at(x, y):
        connection.sync()
        while connection.pending_events():
            connection.next_event()
        move(x, y)
        command('button 1')
        command('button 0')
        result = []
        while connection.pending_events():
            item = connection.next_event()
            if item.type == X.ButtonPress:
                result.append(item.window.id)
            if item.type == X.ClientMessage and item.client_type == atom('WM_PROTOCOLS'):
                assert item.data[1][0] != atom('WM_DELETE_WINDOW'), 'Cut caption corner closed the application'
        return result

    def state(w, action, *names):
        atoms = [atom('_NET_WM_STATE_' + n) for n in names]
        root.send_event(event.ClientMessage(window=w, client_type=atom('_NET_WM_STATE'),
            data=(32, [action, *atoms, *([0] * (2 - len(atoms))), 2, 0])),
            event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
        connection.sync()
        time.sleep(.35)

    try:
        response('READY')
        backdrop = window('Native backdrop', 0, 0, size.width, size.height,
            kind='native-backdrop', color=0x1e6432)
        main = window('Windows main', 180, 160, 320, 200, frame=True)
        corners(main, 'main-initial', framed=True, rounded=not baseline)
        if baseline:
            return observations
        g = main.get_geometry()
        assert main.id not in click_at(g.x + g.width - 1, g.y - 23)
        corners(main, 'main-after-cut-click', framed=True)
        main.configure(x=240, y=190, width=260, height=180)
        connection.sync()
        time.sleep(.25)
        corners(main, 'main-resized', framed=True)
        state(main, 1, 'MAXIMIZED_HORZ', 'MAXIMIZED_VERT')
        g = main.get_geometry()
        assert g.width == size.width
        image = capture('main-maximized')
        assert image.getpixel((0, 0)) == (234, 241, 248)
        assert image.getpixel((size.width - 1, 0)) == (234, 241, 248)
        assert image.getpixel((0, size.height - 1)) == CLIENT
        assert image.getpixel((size.width - 1, size.height - 1)) == CLIENT
        state(main, 0, 'MAXIMIZED_HORZ', 'MAXIMIZED_VERT')
        corners(main, 'main-unmaximized', framed=True)
        state(main, 1, 'FULLSCREEN')
        g = main.get_geometry()
        assert (g.width, g.height) == (size.width, size.height)
        image = capture('main-fullscreen')
        assert all(image.getpixel(p) == CLIENT for p in ((0, 0), (size.width - 1, 0),
            (0, size.height - 1), (size.width - 1, size.height - 1)))
        state(main, 0, 'FULLSCREEN')
        corners(main, 'main-unfullscreen', framed=True)
        root.send_event(event.ClientMessage(window=main, client_type=atom('WM_CHANGE_STATE'),
            data=(32, [3, 0, 0, 0, 0])),
            event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
        connection.sync()
        time.sleep(.25)
        assert main.get_wm_state().state == Xutil.IconicState
        activate(main)
        corners(main, 'main-unminimized', framed=True)
        os.kill(int(env['KARYO_TEST_SERVER_PID']), signal.SIGHUP)
        time.sleep(.3)
        corners(main, 'main-reconfigured', framed=True)
        owner = window('Unmapped owner', 0, 0, 1, 1, override=True, mapped=False)
        main.set_wm_transient_for(owner)
        main.set_wm_hints(flags=1 | 64, input=1, window_group=owner)
        connection.sync()
        tool = window('Owned tool', 650, 250, 240, 140, parent=owner, window_type='DIALOG')
        corners(tool, 'tool-initial')
        g = tool.get_geometry()
        recipients = click_at(g.x, g.y)
        assert tool.id not in recipients and backdrop.id in recipients, recipients
        activate(tool)
        recipients = click_at(g.x + 12, g.y + 12)
        assert tool.id in recipients, recipients
        tool.configure(x=700, y=270, width=200, height=160)
        connection.sync()
        time.sleep(.25)
        corners(tool, 'tool-resized')
        for kind in ('NORMAL', 'POPUP_MENU', 'TOOLTIP', 'UTILITY'):
            tool.change_property(atom('_NET_WM_WINDOW_TYPE'), Xatom.ATOM, 32,
                [atom('_NET_WM_WINDOW_TYPE_' + kind)])
            connection.sync()
            time.sleep(.2)
            corners(tool, 'tool-type-' + kind.lower(), rounded=kind == 'UTILITY')
        tool.delete_property(atom('_NET_WM_WINDOW_TYPE'))
        connection.sync()
        time.sleep(.2)
        corners(tool, 'tool-type-removed', rounded=False)
        tool.change_property(atom('_NET_WM_WINDOW_TYPE'), Xatom.ATOM, 32,
            [atom('_NET_WM_WINDOW_TYPE_UTILITY')])
        connection.sync()
        time.sleep(.2)
        corners(tool, 'tool-type-restored')
        tool.delete_property(atom('WM_TRANSIENT_FOR'))
        connection.sync()
        time.sleep(.2)
        corners(tool, 'tool-no-owner', rounded=False)
        tool.set_wm_transient_for(owner)
        connection.sync()
        time.sleep(.2)
        corners(tool, 'tool-owner-restored')
        tool.set_wm_class('native-tool', 'native-tool')
        connection.sync()
        time.sleep(.2)
        corners(tool, 'tool-native-class', rounded=False)
        tool.set_wm_class('fixture.EXE', 'fixture.EXE')
        connection.sync()
        time.sleep(.2)
        corners(tool, 'tool-wine-class-restored')
        alpha = window('Opaque ARGB custom UI', 950, 250, 180, 140, alpha=True,
            parent=owner, window_type='DIALOG', color=0xff112233)
        corners(alpha, 'alpha-custom-ui', rounded=False)
        override = window('Override popup', 950, 450, 180, 140, override=True,
            parent=owner, window_type='DIALOG')
        corners(override, 'override-popup', rounded=False)
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
        for w in reversed(created):
            w.destroy()
        connection.sync()
        connection.close()


if __name__ == '__main__':
    path = Path(__file__).with_name('caption-buttons.py')
    spec = importlib.util.spec_from_file_location('private_frame_harness', path)
    harness = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(harness)
    harness.main(checker=checks, description=__doc__, actor_path=Path(__file__),
        config_xml='<labwc_config><focus><followMouse>no</followMouse></focus>'
            '<theme><dropShadows>no</dropShadows></theme></labwc_config>')
