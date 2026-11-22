#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check newly registered PE icons, native captions and both foreign protocols."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time
from PIL import Image
from Xlib import X, Xatom, display, error
from Xlib.protocol import event


def checks(env, output, baseline, *, panel=False):
    sys.path.insert(0, '/usr/libexec/karyo/win32')
    from karyo_win32.desktop import register_application
    connection = display.Display(env['DISPLAY'])
    root = connection.screen().root
    atom = connection.intern_atom
    data = Path(env['XDG_DATA_HOME'])
    menu = data / 'applications'
    assert not menu.exists(), 'The compositor must start before the user applications directory exists'
    work = output / 'probe'
    work.mkdir()
    protocols = {'wlr-icons':Path(__file__).with_name('wlr-icons.xml'),
        'ext-icons':Path('/usr/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml')}
    for name, xml in protocols.items():
        for mode, suffix in [('client-header', '.h'), ('private-code', '.c')]:
            subprocess.run(['wayland-scanner', mode, str(xml), str(work / (name + suffix))], check=True)
    flags = shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','wayland-client'], text=True))
    probe = work / 'foreign-icons'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(work),
        str(Path(__file__).with_name('foreign-icons-probe.c')),
        *[str(work / (name + '.c')) for name in protocols],*flags,'-o',str(probe)],check=True)
    created, observations = [], []
    shell = None
    shell_log = None

    def settle():
        connection.sync()
        time.sleep(.25)

    def window(title, kind):
        w = root.create_window(200,180,400,220,0,connection.screen().root_depth,
            X.InputOutput,X.CopyFromParent,background_pixel=0x112233)
        created.append(w)
        w.set_wm_name(title)
        w.set_wm_class(kind,kind)
        w.change_property(atom('_NET_WM_PID'), Xatom.CARDINAL, 32, [os.getpid()])
        w.map()
        settle()
        return w

    def destroy(w):
        w.destroy()
        created.remove(w)
        settle()

    def check(w, title, kind, expected, color, label):
        root.send_event(event.ClientMessage(window=w,client_type=atom('_NET_ACTIVE_WINDOW'),
            data=(32,[2,X.CurrentTime,0,0,0])),event_mask=X.SubstructureRedirectMask|X.SubstructureNotifyMask)
        settle()
        rows = subprocess.check_output([str(probe)],env=env,text=True,timeout=5).splitlines()
        found = {}
        for row in rows:
            protocol,name,app = row.split('\t')
            if name==title:found[protocol]=app
        assert found=={'wlr':expected,'ext':expected}, (label,found,expected)
        assert w.get_wm_class()==(kind if isinstance(kind,tuple) else (kind,kind)), 'Foreign remapping changed the raw X11 identity'
        p = output / (label+'.png')
        deadline=time.monotonic()+2.5
        while True:
            subprocess.run(['grim',str(p)],env=env,check=True,timeout=5)
            g=w.get_geometry()
            with Image.open(p) as image:
                rgb=image.convert('RGB')
            client_pixel=rgb.getpixel((g.x+g.width//2,g.y+g.height//2))
            if not label.startswith('real-pe') or client_pixel==(25,105,175):break
            assert time.monotonic()<deadline,(label,'Real GDI client did not paint',client_pixel)
            time.sleep(.05)
        pixels=[rgb.getpixel((g.x+x,g.y-12)) for x in (10,11,12,13)]
        if color is not None:assert all(pixel==color for pixel in pixels),(label,pixels,color)
        row={'stage':label,'foreign':found,'raw_class':kind,'caption_pixels':pixels,
            'frame_sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
        if label.startswith('real-pe'):
            row['real_gdi_client_pixel']=client_pixel
        if panel and color is not None:
            assert shell.poll() is None, 'Actual shell exited during application registration'
            # The taskbar is a layer-shell surface in the bottom 48 pixels.
            # A single 28x28 resource tile must be present even for a window group.
            points=[(x,y) for y in range(rgb.height-48,rgb.height) for x in range(rgb.width)
                if rgb.getpixel((x,y))==color]
            assert len(points)>=600, (label,'Actual taskbar resource pixels missing',len(points))
            xs=[p[0] for p in points];ys=[p[1] for p in points]
            bounds=[min(xs),min(ys),max(xs),max(ys)]
            assert bounds[2]-bounds[0]<32 and bounds[3]-bounds[1]<32, (label,'Duplicated resource tiles',bounds)
            row.update(taskbar_resource_pixels=len(points),taskbar_resource_bounds=bounds)
        observations.append(row)

    def register(identifier='qa-icons'):
        exe=Path('/usr/share/karyo-qa/runtime-probe.exe')
        context={'storage':str(Path(env['HOME'])/'storage'),'applicationId':identifier,'ownerUid':os.getuid(),'file':str(exe)}
        register_application(context,env,Path(env['HOME']))
        entry=menu/('karyo-win32-'+identifier+'.desktop')
        icon=Path(next(line[5:] for line in entry.read_text().splitlines() if line.startswith('Icon=')))
        assert icon.is_file(), 'Actual PE resource extraction failed'
        with Image.open(icon) as image:assert image.convert('RGB').getpixel((16,16))==(255,128,0)
        return entry, icon

    try:
        if panel:
            shell_log = (output / 'shell.log').open('wb')
            shell = subprocess.Popen(['/usr/bin/karyo-shell'], env=env,
                stdout=shell_log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 12
            while True:
                assert shell.poll() is None, 'Actual Karyo shell exited; inspect shell.log'
                query = subprocess.run(['dbus-send','--session','--print-reply',
                    '--dest=org.freedesktop.DBus','/org/freedesktop/DBus',
                    'org.freedesktop.DBus.NameHasOwner','string:org.karyo.Shell'],
                    env=env,capture_output=True,text=True,timeout=3)
                if query.returncode==0 and 'boolean true' in query.stdout:break
                assert time.monotonic()<deadline, 'Actual Karyo shell did not claim its bus name'
                time.sleep(.1)
            time.sleep(.6)
        # Force an initial miss before any launcher registration.
        w=window('Before registration','runtime-probe.exe')
        check(w,'Before registration','runtime-probe.exe','runtime-probe.exe',None,'before-registration')
        destroy(w)
        entry,icon=register()
        w=window('After registration','runtime-probe.exe')
        check(w,'After registration','runtime-probe.exe',
            'runtime-probe.exe' if baseline else 'karyo-win32-qa-icons',
            None if baseline else (255,128,0),'fresh-registration')
        if baseline:return observations
        if panel:
            second=window('Same application group','runtime-probe.exe')
            check(second,'Same application group','runtime-probe.exe','karyo-win32-qa-icons',
                (255,128,0),'two-window-group')
            destroy(second)
        destroy(w)
        # Same entry, atomic replacement and a new icon path: no compositor reload.
        replacement=icon.with_name('replacement.png')
        Image.new('RGB',(32,32),(40,180,220)).save(replacement)
        temporary=menu/'replacement.tmp'
        temporary.write_text(entry.read_text().replace(str(icon),str(replacement)))
        temporary.replace(entry)
        w=window('Atomic update','runtime-probe.exe')
        check(w,'Atomic update','runtime-probe.exe','karyo-win32-qa-icons',(40,180,220),'atomic-update')
        destroy(w)
        saved=data/'saved-applications';menu.rename(saved)
        w=window('Directory removed','runtime-probe.exe')
        check(w,'Directory removed','runtime-probe.exe','runtime-probe.exe',None,'directory-removed')
        destroy(w)
        saved.rename(menu)
        w=window('Directory restored','runtime-probe.exe')
        check(w,'Directory restored','runtime-probe.exe','karyo-win32-qa-icons',(40,180,220),'directory-restored')
        # A late class update changes foreign identity, leaving native Linux classes intact.
        w.set_wm_class('native-icons','native-icons');settle()
        check(w,'Directory restored','native-icons','native-icons',None,'native-class')
        w.set_wm_class('runtime-probe.EXE','runtime-probe.EXE');settle()
        check(w,'Directory restored','runtime-probe.EXE','karyo-win32-qa-icons',(40,180,220),'late-exe-class')
        destroy(w)
        # Existing applications outside the Karyo launcher namespace retain raw foreign IDs.
        entry.unlink()
        (menu/'ordinary.desktop').write_text('[Desktop Entry]\nType=Application\nName=Ordinary\nExec=true\nStartupWMClass=ordinary.exe\nIcon='+str(icon)+'\n')
        w=window('Ordinary entry','ordinary.exe')
        check(w,'Ordinary entry','ordinary.exe','ordinary.exe',(255,128,0),'ordinary-desktop')
        destroy(w)
        # Run the actual public launcher, registration and Wine process, with both PE architectures.
        wine_env={**env,'WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d','LANG':'C.UTF-8'}
        for bits in (64,32):
            public=Path('/usr/share/karyo-qa/runtime-probe'+('32' if bits==32 else '')+'.exe')
            exe=Path(env['HOME'])/('RealIcon'+str(bits)+'.exe')
            shutil.copyfile(public,exe)
            log_path=output/('real-pe'+str(bits)+'.log')
            with log_path.open('wb') as log:
                application=subprocess.Popen(['/usr/bin/karyo-win32','--app-id','qa-real-icons',str(exe),'--show-window'],
                    env=wine_env,stdout=log,stderr=subprocess.STDOUT)
                try:
                    deadline=time.monotonic()+75
                    real=None
                    while real is None:
                        assert application.poll() is None and time.monotonic()<deadline,'Actual PE window missing; inspect '+str(log_path)
                        for candidate in root.query_tree().children:
                            try:
                                if candidate.get_wm_name()=='Karyo Win32 QA':real=candidate;break
                            except error.XError:pass
                        time.sleep(.04)
                    settle()
                    check(real,'Karyo Win32 QA',real.get_wm_class(),'karyo-win32-qa-real-icons',
                        (255,128,0),'real-pe'+str(bits))
                    assert application.wait(timeout=12)==0
                finally:
                    if application.poll() is None:
                        application.terminate()
                        application.wait(timeout=5)
        return observations
    finally:
        if shell is not None and shell.poll() is None:
            shell.terminate()
            try:shell.wait(timeout=5)
            except subprocess.TimeoutExpired:
                shell.kill();shell.wait(timeout=3)
        if shell_log is not None:shell_log.close()
        for w in created:
            try:w.destroy()
            except error.XError:pass
        connection.sync()
        connection.close()


if __name__=='__main__':
    path=Path(__file__).with_name('caption-buttons.py')
    spec=importlib.util.spec_from_file_location('caption_harness',path)
    harness=importlib.util.module_from_spec(spec);spec.loader.exec_module(harness)
    harness.main(checker=checks,description=__doc__,actor_path=Path(__file__),
        private_pointer=False,real_wine_application=True)
