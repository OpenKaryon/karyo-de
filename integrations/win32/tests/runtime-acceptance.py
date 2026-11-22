#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Installed native Wine64 + real PE/MSI/Qt QA; disposable ordinary-user image only."""
import argparse,hashlib,json,os,pwd,shutil,signal,stat,subprocess,sys,tempfile,time
from pathlib import Path
from PIL import Image
from Xlib import X,Xatom,display,error


def require(value,message):
    if not value:raise RuntimeError(message)


def run(args,**kwargs):return subprocess.run(args,check=True,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=150,**kwargs)


def latest_record(home,identifier):
    rows=[]
    for p in (home/'.local/share/karyo/windows/logs').glob('*.json'):
        data=json.loads(p.read_text())
        if data['applicationId']==identifier:rows.append((p.name,data))
    require(bool(rows),'Missing private launch result');return sorted(rows)[-1][1]


def native_status(text):
    text=text.replace('\r','');require('KARYO_STATUS_BEGIN\n' in text and 'KARYO_STATUS_END' in text,'Win32 native status report missing')
    body=text.split('KARYO_STATUS_BEGIN\n',1)[1].split('KARYO_STATUS_END',1)[0]
    fields=dict(line.split(':',1) for line in body.splitlines() if ':' in line)
    require(all(int(x)==os.getuid() for x in fields['Uid'].split()),'Wine native report has another UID')
    require(fields['NoNewPrivs'].strip()=='1' and all(int(fields[k],16)==0 for k in ('CapEff','CapPrm','CapInh','CapAmb')),'Wine native process retained privileges')
    return {'uid':os.getuid(),'no_new_privs':True,'current_capabilities':'zero','native_name':fields['Name'].strip()}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    require(os.getuid()==1001 and pwd.getpwuid(os.getuid()).pw_name=='win32qa','Use the disposable ordinary Wine QA user')
    require(os.environ.get('DBUS_SESSION_BUS_ADDRESS','').startswith('unix:'),'A real private D-Bus session is required')
    args.output.mkdir(exist_ok=False,parents=True);home=Path(pwd.getpwuid(os.getuid()).pw_dir)
    require(not (home/'.local').exists(),'Wine QA home must be fresh')
    public=Path('/usr/share/karyo-qa/runtime-probe.exe');require(public.is_file(),'Compiled PE fixture missing')
    exe=home/'Пример $(echo nope).EXE';shutil.copyfile(public,exe)
    base={'LANG':'C.UTF-8','LC_ALL':'C.UTF-8','WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d',
          'QT_QPA_PLATFORM':'wayland','QT_STYLE_OVERRIDE':'karyo','QT_PLUGIN_PATH':'/does/not/exist/linux-plugins'}
    report={'uid':os.getuid(),'cases':[],'wine_version':run(['/usr/bin/wine','--version']).stdout.strip()}
    inspection=run(['/usr/bin/karyo-win32','--inspect',str(exe)],env={**os.environ,**base,'HOME':'/not-the-real-home','OPENKARYON_SESSION_SLOT':'2'})
    inspected=json.loads(inspection.stdout);require(inspected['ownerUid']==1001 and str(home) in inspected['prefix'],'Inspection selected a foreign account')
    require(not (home/'.local').exists(),'Inspection wrote user storage');report['cases'].append('read-only inspection uses native account')
    with tempfile.TemporaryDirectory(prefix='karyo-win32-engine-') as tmp:
        work=Path(tmp);runtime=work/'runtime';runtime.mkdir(mode=0o700);config=work/'config';config.mkdir();display_file=work/'x-display'
        (config/'rc.xml').write_text('<labwc_config><focus><followMouse>no</followMouse></focus></labwc_config>')
        (config/'autostart').write_text('#!/bin/sh\nprintf "%s\\n" "$DISPLAY" > "'+str(display_file)+'"\n');(config/'autostart').chmod(0o700)
        env={**os.environ,**base,'XDG_RUNTIME_DIR':str(runtime),'XDG_CONFIG_HOME':str(work/'xdg'),'WLR_BACKENDS':'headless','WLR_RENDERER':'pixman','WLR_HEADLESS_OUTPUTS':'1','XDG_CURRENT_DESKTOP':'Karyo'}
        for key in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','LABWC_PID','XAUTHORITY'):env.pop(key,None)
        connection=None;application=None
        with (args.output/'compositor.log').open('wb') as server_log:
            server=subprocess.Popen(['/usr/bin/labwc','-C',str(config)],env=env,stdout=server_log,stderr=subprocess.STDOUT,start_new_session=True)
            try:
                deadline=time.monotonic()+10
                while not display_file.exists():require(server.poll() is None and time.monotonic()<deadline,'Private compositor startup failed');time.sleep(.05)
                env['DISPLAY']=display_file.read_text().strip();env['WAYLAND_DISPLAY']=next(p.name for p in runtime.glob('wayland-*') if p.is_socket())
                connection=display.Display(env['DISPLAY']);root=connection.screen().root
                written=home/'Результат Пример.txt';windows_path='Z:'+str(written).replace('/','\\')
                arguments=['--show-window','--write-file',windows_path,'a b','$(touch nope)','Пример']
                with (args.output/'first-launch.log').open('wb') as log:
                    application=subprocess.Popen(['/usr/bin/karyo-win32','--app-id','qa-suite',str(exe),*arguments],env=env,stdout=log,stderr=subprocess.STDOUT)
                    deadline=time.monotonic()+150;window=None
                    while window is None:
                        require(application.poll() is None and time.monotonic()<deadline,'Real Windows window did not appear; see private engine log')
                        for candidate in root.query_tree().children:
                            try:
                                if candidate.get_wm_name()=='Karyo Win32 QA':window=candidate;break
                            except error.XError:pass
                        time.sleep(.05)
                    geometry=window.get_geometry();raw_class=window.get_full_property(connection.intern_atom('WM_CLASS'),Xatom.STRING)
                    classes=raw_class.value.decode('utf-8',errors='replace').strip('\0').split('\0') if raw_class is not None else []
                    run(['/usr/bin/grim',str(args.output/'win32-window.png')],env=env)
                    with Image.open(args.output/'win32-window.png') as image:
                        require(image.convert('RGB').getpixel((geometry.x+geometry.width//2,geometry.y+geometry.height//2))==(25,105,175),'Win32 GDI pixels are not rendered')
                    report['window']={'title':'Karyo Win32 QA','wm_class':classes,'gdi_pixels':True}
                    require(application.wait(timeout=30)==0,'Real PE launcher failed');application=None
                record=latest_record(home,'qa-suite');require(record['returnCode']==0,'Wrong successful engine status')
                text=Path(record['log']).read_text(errors='replace');report['native_process']=native_status(text)
                require('KARYO_QT_PLATFORM=0' in text and 'KARYO_QT_STYLE=0' in text,'Linux Qt environment leaked into Windows')
                require('KARYO_BUS='+env['DBUS_SESSION_BUS_ADDRESS'] in text,'Private D-Bus address was changed')
                for index,value in enumerate(arguments,1):require('KARYO_ARG_'+str(index)+'='+value in text,'PE argument changed')
                require(written.read_text()=='Karyo Windows UTF8: Пример\n','Win32 Unicode file I/O failed')
                report['cases'].append('real PE window/pixels/Unicode/arguments/environment/capabilities')
                entry=home/'.local/share/applications/karyo-win32-qa-suite.desktop';entry_text=entry.read_text()
                require('Exec=karyo-win32 --gui --registered qa-suite\n' in entry_text and str(exe) not in entry_text,'Menu Exec interpolates file path')
                icon=next(line[5:] for line in entry_text.splitlines() if line.startswith('Icon='));require(Path(icon).is_file(),'Real PE icon was not extracted')
                with Image.open(icon) as image:require(image.size==(32,32),'Unexpected extracted resource icon')
                report['cases'].append('real PE icon and stable menu registration')
                run(['/usr/bin/karyo-win32','--registered','qa-suite'],env=env)
                require(latest_record(home,'qa-suite')['prefix']==record['prefix'],'Registered launch changed prefix')
                failed=subprocess.run(['/usr/bin/karyo-win32','--app-id','qa-suite','--no-register',str(exe),'--exit42'],env=env,text=True,capture_output=True,timeout=30)
                require(failed.returncode==42 and latest_record(home,'qa-suite')['returnCode']==42,'Windows exit status was lost')
                report['cases'].append('registered reuse and real exit42')
                installer='/usr/share/karyo-qa/runtime-probe.msi'
                run(['/usr/bin/karyo-win32','--app-id','qa-installer',installer,'/qn'],env=env)
                installed_prefix=home/'.local/share/karyo/windows/prefixes/qa-installer'
                installed=list((installed_prefix/'drive_c').rglob('InstalledProbe.exe'));require(len(installed)==1,'Real MSI did not install executable')
                require(hashlib.sha256(installed[0].read_bytes()).digest()==hashlib.sha256(public.read_bytes()).digest(),'MSI installed different payload')
                run(['/usr/bin/karyo-win32',str(installed[0])],env=env)
                require(latest_record(home,'qa-installer')['prefix']==str(installed_prefix),'Installed EXE did not inherit MSI prefix')
                require(installed_prefix!=Path(record['prefix']),'Independent application profiles collided')
                report['cases'].append('real MSI installation and inherited EXE profile')
                for p in (home/'.local/share/karyo/windows/logs').iterdir():require(stat.S_IMODE(p.stat().st_mode)==0o600,'Private diagnostics mode changed')
                run(['/usr/bin/dpkg','-V','karyo-win32','labwc']);report['cases'].append('installed package integrity')
            finally:
                diagnostics=args.output/'engine-logs';diagnostics.mkdir(exist_ok=True)
                for item in (home/'.local/share/karyo/windows/logs').glob('*'):
                    if item.is_file() and not item.is_symlink() and item.stat().st_uid==os.getuid():
                        with item.open('rb') as stream:
                            stream.seek(max(0,item.stat().st_size-262144));data=stream.read(262144)
                        (diagnostics/item.name).write_bytes(data)
                (args.output/'progress.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
                if application is not None and application.poll() is None:application.terminate();application.wait(timeout=5)
                if connection is not None:connection.close()
                if server.poll() is None:
                    os.killpg(server.pid,signal.SIGTERM)
                    try:server.wait(timeout=8)
                    except subprocess.TimeoutExpired:os.killpg(server.pid,signal.SIGKILL);server.wait(timeout=3)
    report['accepted']=True;(args.output/'acceptance.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n');print(json.dumps(report,ensure_ascii=False))


if __name__=='__main__':main()
