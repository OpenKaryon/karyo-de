#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual installed native Qt/MIME lifecycle; Linux engine fixture is explicit."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import pwd
import shlex
import signal
import subprocess
import time


def wait(test, label, seconds=20):
    end=time.monotonic()+seconds
    while True:
        found=test()
        if found:return found
        assert time.monotonic()<end,label
        time.sleep(.08)


def checks(env, output, baseline):
    assert not baseline and os.getuid()==1001 and pwd.getpwuid(1001).pw_dir=='/home/win32qa'
    env={**env,'QT_QPA_PLATFORM':'wayland','XDG_CURRENT_DESKTOP':'Karyo',
         'XDG_SESSION_TYPE':'wayland','LANG':'ru_RU.UTF-8','LC_ALL':'ru_RU.UTF-8',
         'PATH':'/usr/bin:/bin','KARYO_MACOS_QA_TAG':str(os.getpid())}
    home=Path('/home/win32qa');assert not (home/'.karyo-macos').exists()
    private=Path(env['HOME']);work=private/'macos-check';work.mkdir()
    protocols={'wlr-icons':Path(__file__).with_name('wlr-icons.xml'),
        'ext-icons':Path('/usr/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml')}
    for name,path in protocols.items():
        for mode,suffix in [('client-header','.h'),('private-code','.c')]:
            subprocess.run(['wayland-scanner',mode,str(path),str(work/(name+suffix))],check=True)
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','wayland-client'],text=True))
    tool=work/'window-action'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(work),
        str(Path(__file__).with_name('window-action-probe.c')),*[str(work/(n+'.c')) for n in protocols],*flags,'-o',str(tool)],check=True)
    observations=[];applications=[];logs=[]

    def command(argv):return subprocess.check_output(argv,env=env,text=True,timeout=8)
    def identities():
        rows={}
        for line in command([str(tool)]).splitlines():
            protocol,title,identifier=line.split('\t')
            if identifier=='karyo-macos':rows[protocol]=title
        return rows if rows=={'wlr':'Приложения macOS','ext':'Приложения macOS'} else None
    def mapped():return bool(identities())
    def close():command([str(tool),'--close','karyo-macos'])
    def screenshot(name):
        p=output/(name+'.png');subprocess.run(['grim',str(p)],env=env,check=True,timeout=5)
        return {'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
    def launch(argv,label,cwd=None):
        log=(output/(label+'.log')).open('wb');logs.append(log)
        p=subprocess.Popen(argv,env=env,cwd=cwd,stdout=log,stderr=subprocess.STDOUT);applications.append(p);return p
    def gui_process():
        found=[]
        for folder in Path('/proc').glob('[0-9]*'):
            try:
                args=(folder/'cmdline').read_bytes().split(b'\0')
                if not args or Path(os.fsdecode(args[0])).name!='karyo-macos-picker':continue
                status=dict(line.split(':',1) for line in (folder/'status').read_text().splitlines() if ':' in line)
                if status['State'].strip().startswith('Z'):continue
                if status['Uid'].split()!=['1001']*4:continue
                variables=(folder/'environ').read_bytes().split(b'\0')
                if ('KARYO_MACOS_QA_TAG='+env['KARYO_MACOS_QA_TAG']).encode() in variables:found.append(int(folder.name))
            except (OSError,ValueError):pass
        assert len(found)<=1,found
        return found[0] if found else None

    try:
        launch(['/usr/bin/karyo-shell'],'actual-shell')
        wait(lambda:'boolean true' in subprocess.run(['dbus-send','--session','--print-reply',
            '--dest=org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.NameHasOwner',
            'string:org.karyo.Shell'],env=env,capture_output=True,text=True,timeout=3).stdout,
            'Actual installed shell did not acquire its session name')
        # Both explicit --gui and no-argument public entry must open the native
        # configured frontend without a runtime; status is visible, not a crash.
        for suffix,argv in [('explicit',['/usr/bin/karyo-macos','--gui']),('no-argument',['/usr/bin/karyo-macos'])]:
            p=launch(argv,'missing-engine-'+suffix)
            wait(identities,'Native translated picker did not map')
            time.sleep(.4);assert p.poll() is None
            shot=screenshot('missing-engine-'+suffix)
            close();assert p.wait(timeout=8)==0
            wait(lambda:not mapped(),'Closed picker remained mapped')
            assert not (home/'.karyo-macos').exists()
            observations.append({'stage':'missing-engine-'+suffix,'native_wayland':True,'foreign':{'wlr':'karyo-macos','ext':'karyo-macos'},**shot})

        probe=Path('/usr/share/karyo-qa/Пример literal $().macho')
        assert 'application/x-karyo-mach-o' in command(['gio','info','-a','standard::content-type',str(probe)])
        default=command(['xdg-mime','query','default','application/x-karyo-mach-o']).strip()
        assert default=='karyo-macos.desktop',default
        config=Path(env['XDG_CONFIG_HOME']);config.mkdir(parents=True,exist_ok=True)
        apps=Path(env['XDG_DATA_HOME'])/'applications';apps.mkdir(parents=True,exist_ok=True)
        override=config/'mimeapps.list';assert not override.exists()
        other=apps/'qa-other.desktop';other.write_text('[Desktop Entry]\nType=Application\nName=QA override\nExec=/bin/false\nMimeType=application/x-karyo-mach-o;\n')
        override.write_text('[Default Applications]\napplication/x-karyo-mach-o=qa-other.desktop;\n')
        assert command(['xdg-mime','query','default','application/x-karyo-mach-o']).strip()=='qa-other.desktop'
        before=override.read_bytes();command(['gio','mime','application/x-karyo-mach-o']);assert override.read_bytes()==before
        override.unlink();other.unlink()
        assert command(['xdg-mime','query','default','application/x-karyo-mach-o']).strip()=='karyo-macos.desktop'
        observations.append({'stage':'mime-handler-and-user-override','mime':'application/x-karyo-mach-o','default':default,'user_override_preserved':True})

        bindir=work/'fixture-bin';bindir.mkdir(mode=0o700)
        engine=bindir/'darling';report=work/'engine.json';stop=work/'fixture.stop'
        engine.write_text('#!/usr/bin/python3 -I\nimport json,os,sys,time\nfrom pathlib import Path\n'
            'row={"fixtureOnly":True,"argv":sys.argv[1:],"pid":os.getpid(),"prefix":os.environ["DPREFIX"],'
            '"status":Path("/proc/self/status").read_text()}\n'
            'Path(os.environ["QA_ENGINE_REPORT"]).write_text(json.dumps(row))\n'
            'print("Explicit Linux engine fixture is running",flush=True)\n'
            'while not Path(os.environ["QA_ENGINE_STOP"]).exists():time.sleep(.05)\n')
        engine.chmod(0o700);env.update(PATH=str(bindir)+':/usr/bin:/bin',QA_ENGINE_REPORT=str(report),QA_ENGINE_STOP=str(stop))
        # Frontend command parsing must preserve both a literal leading --
        # and arguments that Qt itself normally recognizes as platform options.
        literal=['--','-platform','not-a-real-platform','-style','not-a-real-style',"literal '$()'",'one\ntwo']
        relative=work/'-Пример literal $().macho';relative.write_bytes(probe.read_bytes());relative.chmod(0o700)
        public=launch(['/usr/bin/karyo-macos','--gui','--',relative.name,'--',*literal],
            'public-literal-arguments',cwd=work)
        wait(lambda:identities() or public.poll() is not None,'Public argument frontend did not map')
        assert public.poll() is None,'Application arguments were consumed as frontend Qt options'
        wait(lambda:report.exists() and report.stat().st_size>0,'Public fixture did not receive arguments')
        first=json.loads(report.read_text());assert first['argv'][-len(literal):]==literal,first['argv']
        assert '/Volumes/SystemRoot'+str(relative) in first['argv']
        stop.touch()
        def public_closed():
            if public.poll() is not None:return public.returncode==0
            if mapped():close()
            return False
        wait(public_closed,'Public frontend did not close after fixture completion')
        wait(lambda:not mapped(),'Public argument frontend stayed mapped')
        stop.unlink();report.unlink()
        observations.append({'stage':'public-gui-literal-arguments','leading_separator_preserved':True,
            'qt_platform_arguments_preserved':True,'leading_dash_file_preserved':True,'arguments':literal,'fixture_only':True})

        # GIO's launched GUI may inherit stdout. A regular log file avoids
        # waiting for that GUI to close a capture_output pipe after GIO exits.
        log=(output/'gio-launch.log').open('wb');logs.append(log)
        subprocess.run(['gio','open',str(probe)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
        wait(identities,'MIME did not launch the native translated picker')
        pid=wait(gui_process,'MIME frontend process missing')
        wait(lambda:report.exists() and report.stat().st_size>0,'Fixture engine did not start')
        row=json.loads(report.read_text());assert row['fixtureOnly']
        status=dict(line.split(':',1) for line in row['status'].splitlines() if ':' in line)
        assert status['Uid'].split()==['1001']*4
        assert all(int(status[key],16)==0 for key in ('CapInh','CapPrm','CapEff','CapAmb'))
        assert status['NoNewPrivs'].strip()=='1'
        assert row['argv'][0:2]==['shell','/bin/bash']
        assert '/Volumes/SystemRoot'+str(probe) in row['argv']
        prefix=Path(row['prefix']);assert prefix.is_relative_to(home/'.karyo-macos')
        assert not prefix.exists(),'The wrapper must not precreate the engine leaf'
        time.sleep(.25);running=screenshot('mime-running')
        close();time.sleep(.3)
        assert mapped() and gui_process()==pid
        os.kill(row['pid'],0)
        guarded=screenshot('close-guard')
        stop.touch()
        def engine_done():
            try:
                return Path('/proc/'+str(row['pid'])+'/stat').read_text().rsplit(')',1)[1].split()[0]=='Z'
            except FileNotFoundError:return True
        wait(engine_done,'Owned fixture engine did not finish')
        assert mapped();time.sleep(.2);finished=screenshot('finished')
        private_logs=list((prefix.parent/'logs').glob('*.log'));assert len(private_logs)==1
        assert all(b'Karyo exit code: 0' in path.read_bytes() for path in private_logs)
        close();wait(lambda:not mapped() and not gui_process(),'Finished MIME frontend did not close')
        observations.append({'stage':'mime-native-gui-lifecycle','fixture_only':True,'darling_runtime_proven':False,
            'same_gui_survived_close':True,'ordinary_engine_credentials':True,'literal_unicode_path':True,
            'running':running,'guard':guarded,'finished':finished})
        return observations
    finally:
        if 'stop' in locals():stop.touch()
        for p in reversed(applications):
            if p.poll() is None:
                p.terminate()
                try:p.wait(timeout=4)
                except subprocess.TimeoutExpired:p.kill();p.wait(timeout=3)
        if 'tool' in locals() and tool.is_file():
            try:
                if mapped():close()
            except (OSError,subprocess.SubprocessError):pass
        for log in logs:log.close()


if __name__=='__main__':
    spec=importlib.util.spec_from_file_location('caption_harness',Path(__file__).with_name('caption-buttons.py'))
    harness=importlib.util.module_from_spec(spec);spec.loader.exec_module(harness)
    harness.main(checker=checks,description=__doc__,actor_path=Path(__file__),private_pointer=False)
