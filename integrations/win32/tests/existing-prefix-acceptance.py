#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise an existing native Wine prefix with a real PE32 console program."""
import argparse,hashlib,json,os,pwd,shutil,subprocess
from pathlib import Path

def require(value,message):
    if not value:raise RuntimeError(message)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--prefix-arch',choices=('win32','win64'),required=True)
    parser.add_argument('--baseline',action='store_true')
    args=parser.parse_args()
    require(os.getuid()==1001 and pwd.getpwuid(os.getuid()).pw_name=='win32qa','Disposable ordinary Wine QA account required')
    home=Path(pwd.getpwuid(os.getuid()).pw_dir)
    require(not (home/'.local').exists(),'Use a fresh disposable home')
    args.output.mkdir(parents=True,exist_ok=False)
    runtime=home/'qa-runtime';runtime.mkdir(mode=0o700)
    os.environ['XDG_RUNTIME_DIR']=str(runtime)
    for key in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','XAUTHORITY','LABWC_PID'):os.environ.pop(key,None)
    program=Path('/usr/share/karyo-qa/runtime-probe32.exe')
    require(program.is_file(),'Native runtime fixture missing')
    command=['/usr/bin/karyo-win32','--app-id','qa-existing-prefix','--no-register',str(program)]
    context=json.loads(subprocess.check_output(['/usr/bin/karyo-win32','--inspect','--app-id','qa-existing-prefix',str(program)],text=True))
    prefix=Path(context['prefix']);require(prefix.is_relative_to(home),'Prefix escaped QA account')
    prefix.mkdir(parents=True,mode=0o700)
    env={**os.environ,'WINEPREFIX':str(prefix),'WINEARCH':args.prefix_arch,'WINEDLLOVERRIDES':'mscoree,mshtml=d','WINEDEBUG':'-all'}
    report={'prefix_arch':args.prefix_arch,'baseline':args.baseline,'actor_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'cases':[]}
    def run(label,argv,environment):
        with (args.output/(label+'.log')).open('wb') as log:
            result=subprocess.run(argv,env=environment,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        report['cases'].append({'stage':label,'returncode':result.returncode})
        return result.returncode
    try:
        require(run('initialize',['/usr/bin/wine','wineboot','--init'],env)==0,'Native prefix initialization failed')
        server=shutil.which('wineserver')
        if server is None:
            paths=subprocess.check_output(['dpkg-query','-L','wine64','libwine'],text=True).splitlines()
            candidates=sorted({p for p in paths if Path(p).name=='wineserver' and Path(p).is_file() and os.access(p,os.X_OK)})
            require(len(candidates)==1,'Native Wine wineserver is ambiguous or missing');server=candidates[0]
        require(run('wait-initialize',[server,'-w'],env)==0,'Native prefix initialization did not settle')
        registry=prefix/'system.reg'
        require(('#arch='+args.prefix_arch) in registry.read_text(errors='replace'),'Wrong native prefix architecture')
        marker=prefix/'karyo-existing-prefix.txt';marker.write_text('existing user data\n')
        for number in range(2):
            code=run('launch-'+str(number),command,{**os.environ,'WINEARCH':'win64','WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d'})
            require((code!=0) if args.baseline else code==0,'Unexpected real launcher result')
            require(marker.read_text()=='existing user data\n','Existing prefix data was changed')
            require(('#arch='+args.prefix_arch) in registry.read_text(errors='replace'),'Existing architecture was changed')
        logs=args.output/'engine-logs';logs.mkdir()
        for p in (home/'.local/share/karyo/windows/logs').glob('*'):shutil.copyfile(p,logs/p.name)
        text='\n'.join(p.read_text(errors='replace') for p in logs.glob('*.log'))
        require('WINEARCH' in text if args.baseline else text.count('KARYO_WIN32_QA=PASS')==2,'Missing actual engine evidence')
        report['accepted']=True
    finally:
        (args.output/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
if __name__=='__main__':main()
