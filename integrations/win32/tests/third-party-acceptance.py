#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise pinned real Windows programs through the installed public launcher."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import pwd
import selectors
import shlex
import signal
import struct
import subprocess
import sys
import time
import zipfile
from PIL import Image
from Xlib import X, Xatom, Xutil, display, error
from Xlib.protocol import event


def require(value,message):
    if not value:raise RuntimeError(message)


def checks(env,output,baseline):
    require(not baseline,'Third-party acceptance requires the positive runtime')
    sys.path.insert(0,'/usr/libexec/karyo/win32')
    from karyo_win32.policy import wine_environment
    home=Path(pwd.getpwuid(os.getuid()).pw_dir)
    require(os.getuid()==1001 and not (home/'.local').exists(),'Fresh ordinary QA home required')
    rows=json.loads(Path(__file__).with_name('third-party-inputs.json').read_text())['inputs']
    inputs=Path('/inputs')
    for row in rows:
        p=inputs/row['file']
        require(p.stat().st_size==row['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==row['sha256'],'Changed pinned input')
    work=Path(env['HOME'])/'third-party';work.mkdir()
    helper=work/'window-inspector.exe'
    subprocess.run(['x86_64-w64-mingw32-gcc','-std=c11','-Wall','-Wextra','-Werror','-municode',
        str(Path(__file__).with_name('window-inspector.c')),'-o',str(helper)],check=True)
    protocols={'wlr-icons':Path(__file__).with_name('wlr-icons.xml'),
        'ext-icons':Path('/usr/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml')}
    for name,path in protocols.items():
        for mode,suffix in [('client-header','.h'),('private-code','.c')]:
            subprocess.run(['wayland-scanner',mode,str(path),str(work/(name+suffix))],check=True)
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','wayland-client'],text=True))
    foreign=work/'foreign-icons'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(work),
        str(Path(__file__).with_name('foreign-icons-probe.c')),
        *[str(work/(name+'.c')) for name in protocols],*flags,'-o',str(foreign)],check=True)
    connection=display.Display(env['DISPLAY']);root=connection.screen().root;atom=connection.intern_atom
    pointer=None;shell=None;applications=[];logs=[];observations=[]
    runenv={**env,'WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d','LANG':'C.UTF-8','LC_ALL':'C.UTF-8'}
    screen=root.get_geometry()

    def wait(test,message,seconds=8):
        deadline=time.monotonic()+seconds
        while True:
            result=test()
            if result:return result
            require(time.monotonic()<deadline,message)
            time.sleep(.07)

    def start(argv,label):
        log=(output/(label+'.log')).open('wb');logs.append(log)
        application=subprocess.Popen(argv,env=runenv,stdout=log,stderr=subprocess.STDOUT)
        applications.append(application);return application

    def public(executable,identifier,*arguments,register=True):
        return ['/usr/bin/karyo-win32','--app-id',identifier,*([] if register else ['--no-register']),str(executable),*arguments]

    def sync_run(argv,label,seconds=90):
        application=start(argv,label)
        require(application.wait(timeout=seconds)==0,'Public launcher failed: '+label)

    def inspect_context(executable,identifier):
        return json.loads(subprocess.check_output(['/usr/bin/karyo-win32','--inspect','--app-id',identifier,str(executable)],env=runenv,text=True))

    def winpath(path):return 'Z:'+str(path).replace('/','\\')

    def inspect_app(context,name,mode='inspect',value=None):
        args=['/usr/bin/wine',str(helper),name,mode]
        if value is not None:args.append(value)
        result=subprocess.run(args,env=wine_environment(context,runenv),capture_output=True,text=True,timeout=10)
        require(result.returncode==0,'Window inspector failed: '+str((name,mode,result.returncode,result.stderr[-1000:])))
        if mode=='inspect':
            info=json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{')))
            diagnostic=output/('inspector-'+str(len(list(output.glob('inspector-*.json'))))+'.json')
            diagnostic.write_text(json.dumps(info,indent=2)+'\n')
            return info

    def windows(name):
        found=[]
        for w in root.query_tree().children:
            try:
                if w.get_attributes().map_state==X.IsViewable and name.lower() in tuple(c.lower() for c in (w.get_wm_class() or ())):found.append(w)
            except error.XError:pass
        return found

    def main_window(name):
        for w in windows(name):
            prop=w.get_full_property(atom('_NET_WM_WINDOW_TYPE'),Xatom.ATOM)
            kinds=[connection.get_atom_name(int(i)) for i in prop.value] if prop is not None else []
            if '_NET_WM_WINDOW_TYPE_NORMAL' in kinds and not w.get_wm_transient_for():return w
        return None

    def pointer_response(expected):
        with selectors.DefaultSelector() as selector:
            selector.register(pointer.stdout,selectors.EVENT_READ)
            require(selector.select(5),'Private pointer stalled')
            require(pointer.stdout.readline().strip()==expected,'Unexpected private pointer response')

    def pointer_command(line):
        pointer.stdin.write(line+'\n');pointer.stdin.flush();pointer_response('OK')
        connection.sync();time.sleep(.12)

    def move(x,y):pointer_command(f'move {x} {y} {screen.width} {screen.height}')

    def screenshot(label):
        p=output/(label+'.png');move(screen.width-12,screen.height-60)
        subprocess.run(['grim',str(p)],env=env,check=True,timeout=5)
        return {'frame':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}

    def activate(w):
        root.send_event(event.ClientMessage(window=w,client_type=atom('_NET_ACTIVE_WINDOW'),data=(32,[2,X.CurrentTime,0,0,0])),
            event_mask=X.SubstructureRedirectMask|X.SubstructureNotifyMask)
        connection.sync();time.sleep(.2)

    def popup_observation(name):
        for w in windows(name):
            prop=w.get_full_property(atom('_NET_WM_WINDOW_TYPE'),Xatom.ATOM)
            kinds=[connection.get_atom_name(int(i)) for i in prop.value] if prop is not None else []
            if kinds==['_NET_WM_WINDOW_TYPE_POPUP_MENU']:
                hints=w.get_wm_hints();input_=bool(hints.input) if hints and hints.flags&1 else None
                require(input_ is False,'A real application menu requested keyboard focus')
                g=w.get_geometry();return {'window':w.id,'type':kinds,'input':input_,'geometry':[g.x,g.y,g.width,g.height]}
        return None

    def verify_program(row,exe,context,application,document=None):
        name=exe.name;label=row['id'];frame=wait(lambda:main_window(name),'Real application did not map: '+label,60)
        activate(frame);time.sleep(.5)
        require(application.poll() is None,'Real application exited before interaction')
        info=inspect_app(context,name)
        # Wine can expose HMENU without cross-process item data. Locate actual
        # rendered menu words instead of assuming that inspection API works.
        def rendered_menu_centers():
            p=output/(label+'-menu-layout.png')
            subprocess.run(['grim',str(p)],env=env,check=True,timeout=5)
            g=frame.get_geometry()
            with Image.open(p) as image:rgb=image.convert('RGB')
            columns=[]
            for x in range(3,min(g.width-3,550)):
                if any(max(rgb.getpixel((g.x+x,g.y+y)))<100 for y in range(2,19)):
                    columns.append(x)
            groups=[]
            for x in columns:
                if not groups or x-groups[-1][-1]>11:groups.append([x])
                else:groups[-1].append(x)
            words=[group for group in groups if len(group)>2]
            if len(words)<3:return None
            return [(g.x+(word[0]+word[-1])//2,g.y+10) for word in words[:3]]
        points=wait(rendered_menu_centers,'Real menu words did not render: '+label,15)
        entry=Path(env['XDG_DATA_HOME'])/'applications'/('karyo-win32-'+context['applicationId']+'.desktop')
        require(entry.is_file(),'Public launcher entry missing')
        icon=Path(next(line[5:] for line in entry.read_text().splitlines() if line.startswith('Icon=')))
        require(icon.is_file(),'Real PE icon extraction failed')
        with Image.open(icon) as image:icon_size=list(image.size)
        title_prop=frame.get_full_property(atom('_NET_WM_NAME'),atom('UTF8_STRING'))
        title=title_prop.value.decode('utf-8') if title_prop is not None else frame.get_wm_name()
        identities={}
        for line in subprocess.check_output([str(foreign)],env=env,text=True,timeout=5).splitlines():
            protocol,window_title,app_id=line.split('\t')
            if window_title==title:identities[protocol]=app_id
        expected='karyo-win32-'+context['applicationId']
        require(identities=={'wlr':expected,'ext':expected},'Real application identity did not reach both protocols')
        report={'program':label,'bits':row['bits'],'input_sha256':row['sha256'],'raw_class':list(frame.get_wm_class()),
            'prefix':str(Path(context['prefix']).relative_to(home)),'icon_sha256':hashlib.sha256(icon.read_bytes()).hexdigest(),
            'icon_size':icon_size,'foreign':identities,'initial':screenshot(label+'-initial'),'menus':[],
            'menu_centers':[list(point) for point in points],'inspection_menus':info['menus']}
        # Repeated mouse navigation after a real click, not synthetic application callbacks.
        move(*points[0]);pointer_command('button 1');pointer_command('button 0')
        for index in (0,1,2,0,2,1):
            move(*points[index])
            def changed_popup():
                found=popup_observation(name)
                if found and report['menus'] and index!=report['menus'][-1]['index']:
                    if found['geometry'][0]==report['menus'][-1]['geometry'][0]:return None
                return found
            popup=wait(changed_popup,'Menu did not change during real hover: '+label,3)
            report['menus'].append({'index':index,**popup})
        require(len({item['geometry'][0] for item in report['menus']})==3,
            'Hover did not visit three distinct real menu headings')
        report['menu_frame']=screenshot(label+'-menu')
        inspect_app(context,name,'key','ESC');time.sleep(.2)
        # Minimize and restore the same native window; the application must survive.
        root.send_event(event.ClientMessage(window=frame,client_type=atom('WM_CHANGE_STATE'),data=(32,[Xutil.IconicState,0,0,0,0])),
            event_mask=X.SubstructureRedirectMask|X.SubstructureNotifyMask)
        connection.sync()
        wait(lambda:frame.get_wm_state().state==Xutil.IconicState,'Real window did not minimize')
        require(application.poll() is None,'Real application died when minimized')
        activate(frame);wait(lambda:frame.get_wm_state().state==Xutil.NormalState,'Real window did not restore')
        require(frame.id==main_window(name).id,'Restore replaced the original real window')
        report['restored']=screenshot(label+'-restored')
        before={item['handle'] for item in inspect_app(context,name)['windows']}
        if row['product']=='7zip':
            inspect_app(context,name,'key','A-T')
            inspect_app(context,name,'key','O')
        else:
            inspect_app(context,name,'key','C-O')
        child=wait(lambda:next((item for item in inspect_app(context,name)['windows'] if item['handle'] not in before and item['owner']),None),
            'Real owned dialog missing: '+label)
        report['owned_dialog']={'title':child['title'],'class':child['class'],'owner_present':bool(child['owner']),
            **screenshot(label+'-dialog')}
        inspect_app(context,name,'key','ESC')
        wait(lambda:all(item['handle']!=child['handle'] for item in inspect_app(context,name)['windows']),
            'Real owned dialog did not close')
        if document is not None:
            inspect_app(context,name,'key','C-A')
            expected='Karyo real application: Пример '+str(row['bits'])
            inspect_app(context,name,'text',expected)
            inspect_app(context,name,'key','C-S')
            wait(lambda:document.read_text(encoding='utf-8-sig')==expected,'Real Unicode edit/save failed')
            report['unicode_save_sha256']=hashlib.sha256(document.read_bytes()).hexdigest()
            report['saved']=screenshot(label+'-saved')
        inspect_app(context,name,'close')
        require(application.wait(timeout=12)==0,'Real application failed normal close')
        report['normal_exit']=0;observations.append(report)

    try:
        shell=start(['/usr/bin/karyo-shell'],'shell')
        wait(lambda:subprocess.run(['dbus-send','--session','--print-reply','--dest=org.freedesktop.DBus',
            '/org/freedesktop/DBus','org.freedesktop.DBus.NameHasOwner','string:org.karyo.Shell'],
            env=env,capture_output=True,text=True,timeout=3).stdout.find('boolean true')>=0,'Actual shell did not start')
        pointer=subprocess.Popen(['/usr/share/karyo-qa/pointer-probe'],env=env,stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);pointer_response('READY')
        for row in rows:
            identifier='qa-thirdparty-'+row['id'];folder=work/row['id'];folder.mkdir()
            if row['product']=='7zip':
                installer=inputs/row['file'];context=inspect_context(installer,identifier)
                sync_run(public(installer,identifier,'/S'),row['id']+'-install')
                prefix=Path(context['prefix']);candidates=list((prefix/'drive_c').glob('Program Files*/7-Zip/7zFM.exe'))
                require(len(candidates)==1,'Real installer output missing or ambiguous');exe=candidates[0]
                sample=folder/'Пример.txt';sample.write_text('Archive roundtrip: Пример\n',encoding='utf-8')
                archive=folder/'Пример.7z';cli=exe.with_name('7z.exe')
                sync_run(public(cli,identifier,'a','-t7z','-y',winpath(archive),winpath(sample),register=False),row['id']+'-create')
                extracted=folder/'extracted';extracted.mkdir()
                sync_run(public(cli,identifier,'x','-y','-o'+winpath(extracted),winpath(archive),register=False),row['id']+'-extract')
                require((extracted/sample.name).read_bytes()==sample.read_bytes(),'Real archive roundtrip failed')
                context=inspect_context(exe,identifier)
                application=start(public(exe,identifier,winpath(archive)),row['id']+'-gui')
                verify_program(row,exe,context,application)
                observations[-1]['archive_roundtrip_sha256']=hashlib.sha256(archive.read_bytes()).hexdigest()
            else:
                with zipfile.ZipFile(inputs/row['file']) as z:
                    for item in z.infolist():
                        target=folder/item.filename
                        require(target.resolve().is_relative_to(folder.resolve()),'Unsafe portable archive member')
                        require((item.external_attr>>16)&0o170000!=0o120000,'Portable archive symlink refused')
                    z.extractall(folder)
                exe=folder/'notepad++.exe';require(exe.is_file(),'Portable program missing')
                document=folder/'Проект.txt';document.write_text('Initial UTF8: Пример',encoding='utf-8')
                context=inspect_context(exe,identifier)
                application=start(public(exe,identifier,'-multiInst','-nosession',winpath(document)),row['id']+'-gui')
                verify_program(row,exe,context,application,document)
            data=exe.read_bytes();offset=struct.unpack_from('<I',data,60)[0];machine=struct.unpack_from('<H',data,offset+4)[0]
            require(machine==(0x8664 if row['bits']==64 else 0x14c),'Actual third-party PE has wrong architecture')
            observations[-1]['pe_sha256']=hashlib.sha256(data).hexdigest()
            (output/'progress.json').write_text(json.dumps(observations,indent=2)+'\n')
        return observations
    finally:
        if pointer is not None and pointer.poll() is None:
            pointer.stdin.write('quit\n');pointer.stdin.flush();pointer.wait(timeout=5)
        for application in reversed(applications):
            if application.poll() is None:
                application.terminate()
                try:application.wait(timeout=5)
                except subprocess.TimeoutExpired:application.kill();application.wait(timeout=3)
        for log in logs:log.close()
        diagnostics=output/'engine-logs';diagnostics.mkdir()
        for p in (home/'.local/share/karyo/windows/logs').glob('*.log'):
            with p.open('rb') as stream:stream.seek(max(0,p.stat().st_size-131072));data=stream.read(131072)
            (diagnostics/p.name).write_bytes(data)
        connection.close()


if __name__=='__main__':
    path=Path(__file__).with_name('caption-buttons.py')
    spec=importlib.util.spec_from_file_location('caption_harness',path)
    harness=importlib.util.module_from_spec(spec);spec.loader.exec_module(harness)
    harness.main(checker=checks,description=__doc__,actor_path=Path(__file__),
        private_pointer=True,real_wine_application=True)
