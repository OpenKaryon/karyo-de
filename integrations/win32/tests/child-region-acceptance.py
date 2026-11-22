#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe actual Win32 child/MDI shape, renderer pixels and private pointer input."""
import argparse,hashlib,json,os,pwd,select,shutil,signal,struct,subprocess,tempfile,time
from pathlib import Path
from PIL import Image
from Xlib import display

NAMES=['regular','grow','shrink','mdi','mdi-max','mdi-restore','mdi-min','mdi-after-min','explicit-rect','explicit-hole','explicit-reset','layered','no-caption','tiny','popup']
ROUNDED={'regular','grow','shrink','mdi','mdi-restore','mdi-after-min','explicit-reset'}
BACKGROUND=(21,53,87)
BODY=(55,155,85)

def require(value,message):
    if not value:raise RuntimeError(message)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--bits',type=int,choices=(32,64),required=True)
    parser.add_argument('--dpi',type=int,choices=(96,192),required=True)
    parser.add_argument('--baseline',action='store_true')
    parser.add_argument('--baseline-file',type=Path)
    args=parser.parse_args()
    require(os.getuid()==1001 and pwd.getpwuid(os.getuid()).pw_name=='win32qa','Disposable Wine QA account required')
    require(os.environ.get('DBUS_SESSION_BUS_ADDRESS','').startswith('unix:'),'Private bus required')
    home=Path(pwd.getpwuid(os.getuid()).pw_dir);require(not (home/'.local').exists(),'Fresh disposable home required')
    os.umask(0o077);args.output.mkdir(parents=True,exist_ok=False)
    program=Path('/usr/share/karyo-qa/child-region-probe'+('32' if args.bits==32 else '')+'.exe')
    data=program.read_bytes();offset=struct.unpack_from('<I',data,60)[0]
    require(struct.unpack_from('<H',data,offset+4)[0]==(332 if args.bits==32 else 34404),'Wrong actual PE machine')
    report={'bits':args.bits,'dpi':args.dpi,'baseline':args.baseline,'actor_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'producer_source_sha256':hashlib.sha256(Path('/usr/share/karyo-qa/child-region-probe.c').read_bytes()).hexdigest(),'producer_sha256':hashlib.sha256(data).hexdigest(),'cases':[]}
    release=dict(line.split('=',1) for line in Path('/etc/os-release').read_text().splitlines() if '=' in line)
    report['distribution']={key:release[key].strip(chr(34)) for key in ('ID','VERSION_ID','VERSION_CODENAME')}
    expected=None
    if not args.baseline:
        require(args.baseline_file is not None,'Positive acceptance requires the matching native baseline')
        expected=json.loads(args.baseline_file.read_text())
        require(expected['accepted'] and expected['baseline'] and expected['bits']==args.bits and expected['dpi']==args.dpi,'Wrong native baseline case')
        require(all(expected[k]==report[k] for k in ('actor_sha256','producer_source_sha256','distribution')),'Fixture differs from native baseline')
        expected={row['name']:row for row in expected['cases']}
    with tempfile.TemporaryDirectory(prefix='karyo-child-region-') as tmp:
        work=Path(tmp);runtime=work/'runtime';runtime.mkdir(mode=0o700);config=work/'config';config.mkdir();ready=work/'display'
        (config/'rc.xml').write_text('<labwc_config><focus><followMouse>no</followMouse></focus></labwc_config>')
        (config/'autostart').write_text('#!/bin/sh\nprintf "%s\\n" "$DISPLAY" > "'+str(ready)+'"\n');(config/'autostart').chmod(0o700)
        env={**os.environ,'XDG_RUNTIME_DIR':str(runtime),'XDG_CONFIG_HOME':str(work/'xdg'),'WLR_BACKENDS':'headless','WLR_RENDERER':'pixman','WLR_HEADLESS_OUTPUTS':'1','WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d','LANG':'C.UTF-8','LC_ALL':'C.UTF-8'}
        for key in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','XAUTHORITY','LABWC_PID','WINEPREFIX','WINEARCH'):env.pop(key,None)
        application=None;connection=None;wine_env=None;wine_server=None;pointer=None
        with (args.output/'compositor.log').open('wb') as server_log:
            server=subprocess.Popen(['/usr/bin/labwc','-d','-C',str(config)],env=env,stdout=server_log,stderr=subprocess.STDOUT,start_new_session=True)
            try:
                deadline=time.monotonic()+15
                while not ready.exists():
                    require(server.poll() is None and time.monotonic()<deadline,'Private compositor failed');time.sleep(.05)
                env['DISPLAY']=ready.read_text().strip();env['WAYLAND_DISPLAY']=next(p.name for p in runtime.glob('wayland-*') if p.is_socket())
                connection=display.Display(env['DISPLAY']);extent=connection.screen().root.get_geometry()
                report['pointer_trace']=[]
                def pointer_state(command):
                    root=connection.screen().root;state=root.query_pointer();focus=connection.get_input_focus().focus
                    active=root.get_full_property(connection.intern_atom('_NET_ACTIVE_WINDOW'),0)
                    report['pointer_trace'].append({'command':command,'x':state.root_x,'y':state.root_y,'mask':state.mask,'child':getattr(state.child,'id',state.child),'focus':getattr(focus,'id',focus),'active':list(map(int,active.value)) if active else []})
                pointer=subprocess.Popen(['/usr/share/karyo-qa/pointer-probe'],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=server_log,text=True)
                require(select.select([pointer.stdout],[],[],8)[0] and pointer.stdout.readline().strip()=='READY','Private persistent pointer failed')
                def pointer_command(command):
                    require(pointer.poll() is None,'Private pointer exited')
                    pointer.stdin.write(command+'\n');pointer.stdin.flush()
                    require(select.select([pointer.stdout],[],[],8)[0] and pointer.stdout.readline().strip()=='OK','Private pointer did not acknowledge its command')
                    pointer_state(command)
                def click(x,y):
                    pointer_command(f'move {x*2+1} {y*2+1} {extent.width*2} {extent.height*2}')
                    time.sleep(.1)
                    pointer_command('button 1');time.sleep(.03);pointer_command('button 0');time.sleep(.1)
                context=json.loads(subprocess.check_output(['/usr/bin/karyo-win32','--inspect','--app-id','qa-child-region',str(program)],env=env,text=True))
                prefix=Path(context['prefix']);require(prefix.is_relative_to(home),'Wrong prefix owner');prefix.mkdir(parents=True,mode=0o700)
                wine_env={**env,'WINEPREFIX':str(prefix),'WINEARCH':'win64'}
                wine_server=shutil.which('wineserver')
                if wine_server is None:
                    paths=subprocess.check_output(['dpkg-query','-L','wine64','libwine'],text=True).splitlines()
                    candidates=sorted({p for p in paths if Path(p).name=='wineserver' and Path(p).is_file() and os.access(p,os.X_OK)})
                    require(len(candidates)==1,'Native Wine server missing');wine_server=candidates[0]
                with (args.output/'dpi-setup.log').open('wb') as log:
                    subprocess.run(['/usr/bin/wine','reg','add',r'HKCU\Control Panel\Desktop','/v','LogPixels','/t','REG_DWORD','/d',str(args.dpi),'/f'],env=wine_env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=150)
                    subprocess.run([wine_server,'-k'],env=wine_env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
                    subprocess.run([wine_server,'-w'],env=wine_env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
                report['wine_version']=subprocess.check_output(['/usr/bin/wine','--version'],text=True).strip()
                advance=args.output/'advance.txt';advance.write_text('-1\n');windows_advance='Z:'+str(advance).replace('/','\\')
                with (args.output/'launcher.log').open('wb') as app_log:
                    application=subprocess.Popen(['/usr/bin/karyo-win32','--app-id','qa-child-region','--no-register',str(program),windows_advance],env=env,stdout=app_log,stderr=subprocess.STDOUT)
                    deadline=time.monotonic()+360
                    def observations():
                        rows={};inputs=[]
                        for p in (home/'.local/share/karyo/windows/logs').glob('*.log'):
                            for line in p.read_text(errors='replace').splitlines():
                                for tag,store in [('KARYO_ROUND=',rows),('KARYO_ROUND_INPUT=',inputs)]:
                                    if not line.startswith(tag):continue
                                    try:value=json.loads(line[len(tag):])
                                    except json.JSONDecodeError:continue
                                    if isinstance(store,dict):store[value['stage']]=value
                                    else:store.append(value)
                        return rows,inputs
                    while len(report['cases'])<len(NAMES):
                        rows,inputs=observations();index=len(report['cases'])
                        require(time.monotonic()<deadline and application.poll() is None,'Probe stopped before completing stages')
                        if index not in rows:time.sleep(.03);continue
                        row=rows[index];name=row['name'];require(name==NAMES[index] and row['dpi']==args.dpi,'Unexpected native stage or actual DPI')
                        left,top,right,bottom=row['rect'];require(0<=left<right<=extent.width and 0<=top<bottom<=extent.height,'Fixture bounds outside private output')
                        image=args.output/(name+'.png');subprocess.run(['grim',str(image)],env=env,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=8)
                        with Image.open(image) as raw:pixels=raw.convert('RGB');row['corner_background']=[pixels.getpixel(point)==BACKGROUND for point in [(left,top),(right-1,top),(left,bottom-1),(right-1,bottom-1)]];row['center_color']=pixels.getpixel(((left+right)//2,(top+bottom)//2))
                        row['edge_background']=[pixels.getpixel(point)==BACKGROUND for point in [((left+right)//2,top),(left,(top+bottom)//2),((left+right)//2,bottom-1),(right-1,(top+bottom)//2)]]
                        row['frame_sha256']=hashlib.sha256(image.read_bytes()).hexdigest()
                        if name in ROUNDED:
                            require(row['corner_hits']==([1]*4 if args.baseline else [0]*4),'Default region hit testing differs from expected shape')
                            require(row['corner_background']==([False]*4 if args.baseline else [True]*4),'Actual rounded corners were not painted by the parent')
                            require(row['edge_hits']==[1]*4 and row['edge_background']==[False]*4,'Straight child borders were clipped instead of just corners')
                            require(row['center_hit']==1 and tuple(row['center_color'])==BODY,'Default child body is not visible/input-active')
                        elif not args.baseline:
                            require(all(row[key]==expected[name][key] for key in ('corner_hits','corner_background','edge_hits','edge_background','center_hit','region_type')),'Explicit/native exception changed')
                        require(row['region_type']==(2 if name=='explicit-rect' else 3 if name=='explicit-hole' else 0),'Implicit shape changed explicit GetWindowRgn semantics')
                        if name.startswith('mdi'):require(row['extended']&0x40,'Probe is not a true MDI child')
                        if name=='explicit-hole':require(row['center_hit']==0 and tuple(row['center_color'])==BACKGROUND,'Application region hole was lost')
                        if name=='shrink':
                            x0,y0,x1,y1=row['previous'];points=[(x1-2,y1-2),(x0+2,y1-2),(x1-2,y0+2)]
                            row['vacated_background']=[pixels.getpixel(point)==BACKGROUND for point in points]
                            require(all(row['vacated_background']),'Vacated child pixels were not repainted')
                        if name=='mdi-after-min':
                            # Restoring a rectangular minimized MDI icon must erase
                            # its complete old footprint, including all four corners.
                            old=next(item for item in report['cases'] if item['name']=='mdi-min')['rect']
                            vacated=pixels.crop(old)
                            row['vacated_icon_pixels']=sum(pixel!=BACKGROUND for pixel in vacated.getdata())
                            require(row['vacated_icon_pixels']==0,'Restored MDI icon left stale pixels in its old footprint')
                        if name in ('regular','mdi'):
                            # Establish a real active client before testing its corner;
                            # retain one virtual device for the whole session.
                            for attempt in range(2):
                                _,events=observations();offset=len([item for item in events if item['stage']==index])
                                click((left+right)//2,(top+bottom)//2)
                                limit=time.monotonic()+2
                                while time.monotonic()<limit:
                                    _,events=observations();body=[item for item in events if item['stage']==index][offset:]
                                    if body:break
                                    time.sleep(.02)
                                if body and body[0]['target_hit']==1:break
                            if not (body and body[0]['target_hit']==1):
                                subprocess.run(['grim',str(args.output/'failed-body.png')],env=env,check=True,timeout=8)
                            require(body and body[0]['target_hit']==1,'Real body click did not establish an active target')
                            row['activation_body_clicks']=attempt+1
                            _,events=observations();offset=len([item for item in events if item['stage']==index])
                            click(left,top);limit=time.monotonic()+3
                            while True:
                                _,events=observations();matches=[item for item in events if item['stage']==index][offset:]
                                if matches:break
                                require(time.monotonic()<limit,'No actual Win32 corner button-down message');time.sleep(.02)
                            row['pointer_target_hit']=matches[0]['target_hit']
                            require(row['pointer_target_hit']==(1 if args.baseline else 0),'Real corner click did not follow the visual child shape')
                            # Keep any software cursor outside the following frame sample.
                            pointer_command(f'move {extent.width-10} {extent.height-10} {extent.width} {extent.height}')
                            row['cursor_parked']=True
                        report['cases'].append(row);advance.write_text(str(index)+'\n')
                    require(application.wait(timeout=10)==0,'Own Win32 probe did not exit cleanly');application=None
                    text='\n'.join(p.read_text(errors='replace') for p in (home/'.local/share/karyo/windows/logs').glob('*.log'))
                    require('KARYO_ROUND_DONE' in text,'Missing complete native probe marker');report['accepted']=True
            finally:
                logs=args.output/'engine-logs';logs.mkdir(exist_ok=True)
                for p in (home/'.local/share/karyo/windows/logs').glob('*'):
                    if p.is_file() and not p.is_symlink():shutil.copyfile(p,logs/p.name)
                (args.output/'progress.json').write_text(json.dumps(report,indent=2)+'\n')
                if wine_server and wine_env:subprocess.run([wine_server,'-k'],env=wine_env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15)
                if application is not None and application.poll() is None:application.terminate();application.wait(timeout=5)
                if pointer is not None and pointer.poll() is None:
                    pointer.stdin.write('quit\n');pointer.stdin.flush()
                    try:pointer.wait(timeout=3)
                    except subprocess.TimeoutExpired:pointer.terminate();pointer.wait(timeout=3)
                if connection:connection.close()
                if server.poll() is None:
                    os.killpg(server.pid,signal.SIGTERM)
                    try:server.wait(timeout=5)
                    except subprocess.TimeoutExpired:os.killpg(server.pid,signal.SIGKILL);server.wait(timeout=3)
    (args.output/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
