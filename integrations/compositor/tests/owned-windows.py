#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Isolated headless X11 owned-window regression; never connects to the desktop.

Needs the matching native labwc package, Xwayland, grim, python3-xlib and
python3-pil. It verifies/extracts the package and owns a private headless display.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from Xlib import X, Xatom, display
from Xlib.protocol import event
from PIL import Image


def checks(env, output, baseline):
    connection = display.Display(env['DISPLAY'])
    root = connection.screen().root
    atom = connection.intern_atom
    created = []

    def window(name, group=None, primary=False, dimensions=(320, 160), kind='fixture.exe', fixed=True, color=0x112233):
        color = 0x808080 if primary else color
        w = root.create_window(200, 150, *dimensions, 0, connection.screen().root_depth,
                               X.InputOutput, X.CopyFromParent, background_pixel=color)
        created.append(w)
        w.set_wm_name(name); w.set_wm_class(kind, kind)
        w.change_property(atom('_NET_WM_PID'), Xatom.CARDINAL, 32, [os.getpid()])
        w.change_property(atom('_NET_WM_WINDOW_TYPE'), Xatom.ATOM, 32, [atom('_NET_WM_WINDOW_TYPE_NORMAL')])
        w.change_property(atom('_MOTIF_WM_HINTS'), atom('_MOTIF_WM_HINTS'), 32,
                          [3, 62 if primary else 36, 126 if primary else 0, 0, 0])
        if fixed:
            w.set_wm_normal_hints(flags=16 | 32, min_width=dimensions[0], min_height=dimensions[1],
                                  max_width=dimensions[0], max_height=dimensions[1])
        if group:
            w.set_wm_hints(flags=1 | 64, input=1, window_group=group)
            w.set_wm_transient_for(group)
        else:
            w.set_wm_hints(flags=1 | 64, input=1, window_group=w)
        w.map(); connection.sync(); time.sleep(.12)
        return w

    def activate(w):
        root.send_event(event.ClientMessage(window=w, client_type=atom('_NET_ACTIVE_WINDOW'),
            data=(32, [2, X.CurrentTime, 0, 0, 0])),
            event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
        connection.sync(); time.sleep(.18)

    def shot(name, point):
        filename = output / (name + '.png')
        subprocess.run(['grim', str(filename)], env=env, check=True, timeout=5)
        with Image.open(filename) as image:
            pixel=image.convert('RGB').getpixel(point)
        return '#%02x%02x%02x' % pixel

    for case, expected in (('wine', not baseline), ('native', False), ('visible-owner', False),
                           ('multiple-frames', False), ('custom-frame', False)):
        kind = 'native-fixture' if case == 'native' else 'fixture.EXE'
        owner = window('application owner', dimensions=(32, 32) if case == 'visible-owner' else (1, 1), kind=kind)
        main = window('main frame', owner, primary=True, dimensions=(800, 580), kind=kind, fixed=False)
        panel = window('palette', owner, kind=kind)
        if case == 'multiple-frames':
            window('independent document', owner, primary=True, dimensions=(600, 400), kind=kind, fixed=False)
        if case == 'custom-frame':
            window('independent custom frame', owner, dimensions=(600, 400), kind=kind, fixed=False)
        gmain, gpanel = main.get_geometry(), panel.get_geometry()
        left, top = max(gmain.x, gpanel.x), max(gmain.y, gpanel.y)
        right = min(gmain.x + gmain.width, gpanel.x + gpanel.width)
        bottom = min(gmain.y + gmain.height, gpanel.y + gpanel.height)
        assert right - left > 30 and bottom - top > 30, 'Fixture windows do not overlap'
        point = ((left + right) // 2, (top + bottom) // 2)
        activate(panel); activate(main)
        pixel = shot(case, point)
        wanted = '#112233' if expected else '#808080'
        assert pixel == wanted, (case, pixel, wanted)
        assert main.get_wm_transient_for().id == owner.id
        assert panel.get_wm_transient_for().id == owner.id
        if expected:
            # Fixed palettes remain behind unrelated apps, never always-on-top.
            foreign = window('unrelated application', primary=True, dimensions=(1280, 720), kind='foreign', fixed=False)
            activate(foreign)
            assert shot(case + '-foreign', point) == '#808080'
            activate(main)
            assert shot(case + '-returned', point) == '#112233'
            # Replace the first palette with four late-created, distinct
            # floating tools. Exercise the legacy multi-palette layout itself.
            panel.destroy();created.remove(panel);connection.sync()
            palettes=[]
            for index in range(4):
                palette=window('late palette '+str(index),owner,dimensions=(180,100),
                    kind=kind,color=0x112230+index)
                palette.configure(x=gmain.x+40+(index%2)*360,y=gmain.y+80+(index//2)*250)
                connection.sync();time.sleep(.15)
                geometry=palette.get_geometry()
                point_i=(geometry.x+geometry.width//2,geometry.y+geometry.height//2)
                palettes.append((palette,point_i,'#11223'+str(index)))
            for palette,point_i,wanted_i in palettes:
                activate(palette);activate(main)
                assert shot(case+'-late-'+str(palette.id),point_i)==wanted_i
                assert palette.get_wm_transient_for().id==owner.id
            activate(foreign);activate(main)
            for palette,point_i,wanted_i in palettes:
                assert shot(case+'-group-'+str(palette.id),point_i)==wanted_i
            # Scene minimization must preserve every X11 NormalState/handle.
            root.send_event(event.ClientMessage(window=main, client_type=atom('WM_CHANGE_STATE'),
                data=(32, [3, 0, 0, 0, 0])), event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
            connection.sync();time.sleep(.2)
            assert main.get_wm_state().state==1
            for palette,point_i,wanted_i in palettes:
                assert palette.get_wm_state().state==1
                assert shot(case+'-minimized-'+str(palette.id),point_i)=='#808080'
            activate(main)
            for palette,point_i,wanted_i in palettes:
                assert shot(case+'-restored-'+str(palette.id),point_i)==wanted_i
            assert main.get_wm_transient_for().id==owner.id
        print(json.dumps({'case':case, 'paletteAboveMain':expected, 'passed':True}), flush=True)
        retired={w.id for w in created}
        for w in reversed(created):w.destroy()
        created.clear();connection.sync()
        # XSync only reaches Xwayland, not the compositor's WM event loop.
        # Wait for its published retirement before python-xlib reuses XIDs.
        deadline=time.monotonic()+5
        while True:
            clients=root.get_full_property(atom('_NET_CLIENT_LIST'),Xatom.WINDOW)
            live=set(clients.value) if clients is not None else set()
            if not retired & live:break
            if time.monotonic()>=deadline:raise RuntimeError('Compositor did not retire captured X11 windows')
            connection.sync();time.sleep(.05)
        time.sleep(.15)
    connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--baseline',action='store_true',help='Require the old missing Wine grouping, never acceptance')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if os.getuid()==0:raise ValueError('Run native grouping QA as an ordinary user')
    args.output.mkdir(parents=True, exist_ok=False)
    manifest=json.loads((args.package/'manifest.json').read_text())
    for item in manifest['artifacts']:
        name=item['file'];path=args.package/name
        if Path(name).name!=name or path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
            raise ValueError('Changed or unsafe native package artifact')
    with tempfile.TemporaryDirectory(prefix='karyo-wine-window-test-') as temporary:
        private = Path(temporary)
        installed=private/'installed'
        subprocess.run(['dpkg-deb','-x',str(args.package/manifest['binaries']['labwc']),str(installed)],check=True)
        compositor=installed/'usr/bin/labwc'
        runtime = private / 'runtime'; runtime.mkdir(mode=0o700)
        config = private / 'labwc'; config.mkdir()
        display_file = private / 'display'
        (config / 'rc.xml').write_text('<labwc_config><focus><followMouse>no</followMouse></focus></labwc_config>')
        (config / 'autostart').write_text('#!/bin/sh\nprintf "%s\\n" "$DISPLAY" > "' + str(display_file) + '"\n')
        (config / 'autostart').chmod(0o700)
        env = {**os.environ, 'XDG_RUNTIME_DIR':str(runtime), 'XDG_CONFIG_HOME':str(private / 'config'),
               'WLR_BACKENDS':'headless', 'WLR_RENDERER':'pixman', 'WLR_HEADLESS_OUTPUTS':'1',
               'HOME':str(private), 'XDG_DATA_HOME':str(private/'data'), 'XDG_CONFIG_DIRS':str(private/'empty')}
        for key in ('DISPLAY','WAYLAND_DISPLAY','LABWC_PID','XAUTHORITY'):
            env.pop(key, None)
        with (args.output / 'compositor.log').open('w') as log:
            server = subprocess.Popen([str(compositor), '-C', str(config)], env=env,
                stdout=log, stderr=log, start_new_session=True)
            try:
                for _ in range(100):
                    if display_file.exists(): break
                    if server.poll() is not None: raise RuntimeError('Compositor exited')
                    time.sleep(.05)
                env['DISPLAY'] = display_file.read_text().strip()
                env['WAYLAND_DISPLAY'] = next(p.name for p in runtime.glob('wayland-*') if p.is_socket())
                checks(env, args.output, args.baseline)
                (args.output/'acceptance.json').write_text(json.dumps({'uid':os.getuid(),'version':manifest['version'],
                    'image_id':manifest['environment']['image_id'],'baseline_missing_group':args.baseline,
                    'checks':['wine palette stacking','native class unchanged','visible owner unchanged',
                        'multiple frames unchanged','custom frame unchanged'],
                    'group_minimize_restore':not args.baseline,'late_palettes':0 if args.baseline else 4,'real_wine_application':False},indent=2)+'\n')
            finally:
                if server.poll() is None:
                    os.killpg(server.pid, signal.SIGTERM)
                    try:server.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        os.killpg(server.pid,signal.SIGKILL);server.wait(timeout=3)


if __name__ == '__main__': main()
