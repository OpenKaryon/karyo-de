#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read actual default Win32 caption pixels in a disposable native Wine session."""
import argparse,hashlib,json,os,pwd,shutil,signal,struct,subprocess,tempfile,time
from pathlib import Path

def require(value,message):
    if not value:raise RuntimeError(message)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--bits',type=int,choices=(32,64),default=64)
    parser.add_argument('--baseline',action='store_true')
    args=parser.parse_args()
    require(os.getuid()==1001 and pwd.getpwuid(os.getuid()).pw_name=='win32qa','Disposable Wine QA account required')
    require(os.environ.get('DBUS_SESSION_BUS_ADDRESS','').startswith('unix:'),'Private bus required')
    home=Path(pwd.getpwuid(os.getuid()).pw_dir)
    require(not (home/'.local').exists(),'Fresh disposable home required')
    args.output.mkdir(parents=True,exist_ok=False)
    program=Path('/usr/share/karyo-qa/caption-probe'+('32' if args.bits==32 else '')+'.exe')
    pe=program.read_bytes();offset=struct.unpack_from('<I',pe,60)[0]
    require(struct.unpack_from('<H',pe,offset+4)[0]==(332 if args.bits==32 else 34404),'Wrong PE architecture')
    report={'wine_version':subprocess.check_output(['/usr/bin/wine','--version'],text=True).strip(),'baseline':args.baseline,'bits':args.bits,'producer_sha256':hashlib.sha256(pe).hexdigest(),'actor_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    with tempfile.TemporaryDirectory(prefix='karyo-caption-') as tmp:
        work=Path(tmp);runtime=work/'runtime';runtime.mkdir(mode=0o700)
        config=work/'config';config.mkdir();ready=work/'display'
        (config/'rc.xml').write_text('<labwc_config><focus><followMouse>no</followMouse></focus></labwc_config>')
        (config/'autostart').write_text('#!/bin/sh\nprintf "%s\\n" "$DISPLAY" > "'+str(ready)+'"\n')
        (config/'autostart').chmod(0o700)
        env={**os.environ,'XDG_RUNTIME_DIR':str(runtime),'XDG_CONFIG_HOME':str(work/'xdg'),'WLR_BACKENDS':'headless','WLR_RENDERER':'pixman','WLR_HEADLESS_OUTPUTS':'1','WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d'}
        for key in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','LABWC_PID','XAUTHORITY'):env.pop(key,None)
        with (args.output/'compositor.log').open('wb') as log:
            server=subprocess.Popen(['/usr/bin/labwc','-d','-C',str(config)],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            try:
                deadline=time.monotonic()+15
                while not ready.exists():
                    require(server.poll() is None and time.monotonic()<deadline,'Private compositor failed');time.sleep(.05)
                env['DISPLAY']=ready.read_text().strip()
                env['WAYLAND_DISPLAY']=next(p.name for p in runtime.glob('wayland-*') if p.is_socket())
                with (args.output/'launcher.log').open('wb') as output:
                    result=subprocess.run(['/usr/bin/karyo-win32','--no-register','--app-id','qa-captions',str(program)],env=env,stdout=output,stderr=subprocess.STDOUT,timeout=180)
                report['returncode']=result.returncode
                copied=args.output/'engine-logs';copied.mkdir()
                for p in (home/'.local/share/karyo/windows/logs').glob('*'):shutil.copyfile(p,copied/p.name)
                text='\n'.join(p.read_text(errors='replace') for p in copied.glob('*.log'))
                rows={}
                for line in text.splitlines():
                    if line.startswith('KARYO_CAPTION='):
                        name,*values=line.split('=',1)[1].split(',');rows[name]=dict(zip(('red','white','width','height'),map(int,values)))
                report['observations']=rows
                report['done']='KARYO_CAPTION_DONE' in text
                expected={'child','tool','disabled','ordinary'}
                positive=all(rows.get(name,{}).get('red',0)>30 and rows[name]['white']>=6 and rows[name]['width']==rows[name]['height'] and rows[name]['red']*4>=3*rows[name]['width']*rows[name]['height'] for name in ('child','tool'))
                negative=all(rows.get(name,{}).get('red',-1)==0 for name in ('disabled','ordinary'))
                baseline=all(row['red']==0 for row in rows.values())
                report['accepted']=report['returncode']==0 and report['done'] and rows.keys()==expected and negative and (baseline if args.baseline else positive)
                (args.output/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n')
                require(report['accepted'],'Actual caption pixel acceptance failed; inspect retained observations')
            finally:
                if server.poll() is None:
                    os.killpg(server.pid,signal.SIGTERM)
                    try:server.wait(timeout=5)
                    except subprocess.TimeoutExpired:os.killpg(server.pid,signal.SIGKILL);server.wait(timeout=3)
    print(json.dumps(report))
if __name__=='__main__':main()
