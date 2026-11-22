#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe actual standard Wine menu windows and repeated keyboard navigation."""
import argparse,json,os,pwd,shutil,signal,struct,subprocess,tempfile,time
from pathlib import Path
from Xlib import X,Xatom,display,error
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--mouse',action='store_true')
parser.add_argument('--bits',type=int,choices=(32,64),default=64)
parser.add_argument('--baseline',action='store_true');args=parser.parse_args()
assert os.getuid()==1001 and pwd.getpwuid(os.getuid()).pw_name=='win32qa'
assert os.environ.get('DBUS_SESSION_BUS_ADDRESS','').startswith('unix:')
args.output.mkdir(parents=True,exist_ok=False)
home=Path(pwd.getpwuid(os.getuid()).pw_dir)
assert not (home/'.local').exists()
program=Path('/usr/share/karyo-qa/menu-probe'+('32' if args.bits==32 else '')+'.exe')
data=program.read_bytes();offset=struct.unpack_from('<I',data,60)[0]
assert struct.unpack_from('<H',data,offset+4)[0]==(0x14c if args.bits==32 else 0x8664),'Wrong menu fixture architecture'
report={'pe_bits':args.bits,'baseline':args.baseline,'uid':os.getuid(),'observations':[],'mouse':args.mouse}
with tempfile.TemporaryDirectory(prefix='karyo-wine-menu-') as tmp:
    work=Path(tmp);runtime=work/'runtime';runtime.mkdir(mode=0o700)
    config=work/'config';config.mkdir();ready=work/'display'
    (config/'rc.xml').write_text('<labwc_config><focus><followMouse>no</followMouse></focus></labwc_config>')
    (config/'autostart').write_text('#!/bin/sh\nprintf "%s\\n" "$DISPLAY" > "'+str(ready)+'"\n')
    (config/'autostart').chmod(0o700)
    env={**os.environ,'LANG':'C.UTF-8','LC_ALL':'C.UTF-8','XDG_RUNTIME_DIR':str(runtime),
         'WLR_BACKENDS':'headless','WLR_RENDERER':'pixman','WLR_HEADLESS_OUTPUTS':'1',
         'WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d'}
    for key in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','LABWC_PID','XAUTHORITY'):env.pop(key,None)
    connection=None;application=None;input_client=None;seen=set()
    with (args.output/'compositor.log').open('wb') as server_log,(args.output/'launcher.log').open('wb') as app_log:
        server=subprocess.Popen(['/usr/bin/labwc','-C',str(config)],env=env,stdout=server_log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            deadline=time.monotonic()+10
            while not ready.exists():
                if server.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Private compositor did not start')
                time.sleep(.05)
            env['DISPLAY']=ready.read_text().strip()
            env['WAYLAND_DISPLAY']=next(p.name for p in runtime.glob('wayland-*') if p.is_socket())
            connection=display.Display(env['DISPLAY']);root=connection.screen().root
            if args.mouse:
                input_client=subprocess.Popen(['/usr/share/karyo-qa/pointer-probe'],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
                if input_client.stdout.readline().strip()!='READY':raise RuntimeError('Private pointer did not initialize')
            def pointer_command(command):
                input_client.stdin.write(command+'\n');input_client.stdin.flush()
                if input_client.stdout.readline().strip()!='OK':raise RuntimeError('Private pointer rejected input')
            application=subprocess.Popen(['/usr/bin/karyo-win32','--no-register','--app-id','qa-menu',str(program),*(['--mouse'] if args.mouse else [])],env=env,stdout=app_log,stderr=subprocess.STDOUT)
            deadline=time.monotonic()+180;rects={};mouse_step=-1;next_move=0
            while application.poll() is None and time.monotonic()<deadline:
                if args.mouse:
                    for path in (home/'.local/share/karyo/windows/logs').glob('*.log'):
                        for line in path.read_text(errors='replace').splitlines():
                            if line.startswith('KARYO_RECT_'):
                                tag,coords=line.split('=',1);rects[int(tag[-1])]=tuple(int(value) for value in coords.split(','))
                    if len(rects)==3 and time.monotonic()>=next_move and mouse_step<8:
                        mouse_step+=1;x,y=rects[mouse_step%3];extent=root.get_geometry()
                        pointer_command(f'move {x} {y} {extent.width} {extent.height}')
                        if mouse_step==0:
                            time.sleep(.1);pointer_command('button 1');pointer_command('button 0')
                        next_move=time.monotonic()+.5
                for window in root.query_tree().children:
                    try:
                        if window.get_attributes().map_state!=X.IsViewable or window.get_wm_name()=='Karyo Menu QA':continue
                        klass=window.get_wm_class() or ()
                        if program.name not in klass:continue
                        prop=window.get_full_property(connection.intern_atom('_NET_WM_WINDOW_TYPE'),Xatom.ATOM)
                        if prop is None:continue
                        types=[connection.get_atom_name(int(atom)) for atom in prop.value]
                        hints=window.get_wm_hints();wants_input=bool(hints.input) if hints and hints.flags&1 else None
                        signature=(window.id,tuple(types),wants_input)
                        if signature in seen:continue
                        seen.add(signature)
                        report['observations'].append({'id':window.id,'types':types,'input':wants_input})
                        if len(report['observations'])==1:
                            subprocess.run(['/usr/bin/grim',str(args.output/'menu.png')],env=env,check=True,timeout=10)
                    except error.XError:pass
                time.sleep(.02)
            if application.poll() is None:raise RuntimeError('Menu probe timed out')
            report['returncode']=application.returncode
        finally:
            diagnostics=args.output/'engine-logs';diagnostics.mkdir()
            for p in (home/'.local/share/karyo/windows/logs').glob('*'):
                if p.is_file() and not p.is_symlink():
                    with p.open('rb') as stream:stream.seek(max(0,p.stat().st_size-262144));data=stream.read(262144)
                    (diagnostics/p.name).write_bytes(data)
            if application is not None and application.poll() is None:application.terminate();application.wait(timeout=5)
            if input_client is not None and input_client.poll() is None:
                input_client.stdin.write('quit\n');input_client.stdin.flush();input_client.wait(timeout=3)
            if connection is not None:connection.close()
            if server.poll() is None:
                os.killpg(server.pid,signal.SIGTERM)
                try:server.wait(timeout=8)
                except subprocess.TimeoutExpired:os.killpg(server.pid,signal.SIGKILL);server.wait(timeout=3)
logs=''.join(p.read_text(errors='replace') for p in (args.output/'engine-logs').glob('*.log'))
report['menu_sequence']=[int(line.split('=')[1]) for line in logs.splitlines() if line.startswith('KARYO_MENU=')]
report['done']='KARYO_MENU_PROBE_DONE' in logs
report['correct_popup_protocol']=bool(report['observations']) and all(row['types']==['_NET_WM_WINDOW_TYPE_POPUP_MENU'] and row['input'] is False for row in report['observations'])
report['repeated_navigation']=len(report['menu_sequence'])>=6 and set(report['menu_sequence'])=={0,1,2}
report['unpatched_dialog_protocol']=bool(report['observations']) and all(row['types']==['_NET_WM_WINDOW_TYPE_DIALOG'] for row in report['observations'])
report['accepted']=report['done'] and report['returncode']==0 and (report['unpatched_dialog_protocol'] if args.baseline else report['correct_popup_protocol'] and report['repeated_navigation'])
(args.output/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
if not report['accepted']:raise RuntimeError('Actual Wine menu acceptance failed; see retained diagnostics')
