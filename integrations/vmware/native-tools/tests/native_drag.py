#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe real upstream detection-window and drag callback pixels in own Xvfb."""
import argparse,json,os,shlex,subprocess,tempfile,time
from pathlib import Path


def function(source, signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        if end>=len(source):raise ValueError('Unterminated upstream callback')
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__,allow_abbrev=False)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--expect',choices=('stock','patched'),required=True)
    args=parser.parse_args();fixtures=Path(__file__).resolve().parent
    native=args.source.resolve(strict=True)/'open-vm-tools/services/plugins/dndcp'
    for name in ('dragDetWndX11.cpp','dndUIX11.cpp'):assert(native/name).is_file()
    def run(command,**kwargs):
        result=subprocess.run(command,text=True,capture_output=True,timeout=90,**kwargs)
        if result.returncode:raise RuntimeError(str(command[0])+': '+result.stdout+result.stderr)
        return result.stdout.strip()
    flags=shlex.split(run(['pkg-config','--cflags','--libs','gtkmm-3.0','x11','xext']))
    results={};server=None
    with tempfile.TemporaryDirectory(prefix='karyo-vmware-drag-')as directory:
        work=Path(directory);detection=work/'detection';icon=work/'icon'
        run(['g++','-std=c++11','-DGTK3','-DDETWNDTEST','-D__DND_UI_X11_H__','-include','unistd.h',
             '-I'+str(native/'dndGuestBase'),str(native/'dragDetWndX11.cpp'),str(fixtures/'detection_probe.cpp'),'-o',str(detection),*flags])
        body=(native/'dndUIX11.cpp').read_text()
        callback=function(body,'void\nDnDUIX11::OnGtkDragBegin(')
        if args.expect=='patched':callback=function(body,'static gboolean\nClearDragImage(')+callback
        (work/'on-gtk-drag-begin.inc').write_text(callback)
        run(['g++','-std=c++11','-DGTK3','-I'+str(work),str(fixtures/'icon_probe.cpp'),'-o',str(icon),*flags])
        with(work/'display').open('wb')as display,(work/'xvfb.log').open('wb')as log:
            server=subprocess.Popen(['Xvfb','-displayfd',str(display.fileno()),'-screen','0','800x600x24','-nolisten','tcp'],
                                    stdout=log,stderr=log,pass_fds=(display.fileno(),))
            try:
                deadline=time.monotonic()+8
                while not(work/'display').read_text().strip():
                    if server.poll()is not None or time.monotonic()>=deadline:raise RuntimeError('Own Xvfb did not become ready')
                    time.sleep(.05)
                env=dict(os.environ,DISPLAY=':'+(work/'display').read_text().strip(),GDK_BACKEND='x11');env.pop('XAUTHORITY',None)
                for label,command,session in (
                    ('wayland_detection',[str(detection),'rgba'if args.expect=='patched'else'legacy-rgb'],'wayland'),
                    ('x11_detection',[str(detection),'input-only'],'x11'),
                    ('wayland_icon',[str(icon),'transparent'if args.expect=='patched'else'original'],'wayland'),
                    ('x11_icon',[str(icon),'original'],'x11')):
                    results[label]=run(command,env=dict(env,XDG_SESSION_TYPE=session))
            finally:
                server.terminate()
                try:server.wait(timeout=3)
                except subprocess.TimeoutExpired:server.kill();server.wait(timeout=3)
    print(json.dumps({'expected':args.expect,'native_upstream_class_and_callback':True,'isolated_x11_observations':results},indent=2))
    return 0


if __name__=='__main__':raise SystemExit(main())
