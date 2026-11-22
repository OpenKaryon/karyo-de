#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Import a real prototype prefix and retain existing data across launcher upgrade."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import pwd
import shlex
import shutil
import subprocess
import time
from Xlib import X,Xatom,display,error
from Xlib.protocol import event


def module(name,filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value


def checks(env,output,baseline):
    assert not baseline and os.getuid()==1001
    old=module('upgrade_state','prefix-upgrade-state.py')
    home=old.home();assert not (home/'.local').exists()
    subprocess.run(['tar','--no-same-owner','-xpf','/usr/share/karyo-qa/upgrade-home.tar','-C',str(home)],check=True)
    expected=json.loads(Path('/usr/share/karyo-qa/upgrade-before.json').read_text())
    restored=old.snapshot(home)
    differences=[key for key in restored.keys()|expected.keys() if restored.get(key)!=expected.get(key)]
    assert not differences,('Restored pre-upgrade user data differs',differences[:5])
    assert subprocess.check_output(['dpkg-query','-W','-f=${Version}','karyo-win32'],text=True).strip()=='0.1.0-2'
    runenv={**env,'HOME':str(home),'USER':'win32qa','LOGNAME':'win32qa',
        'WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d','OPENKARYON_SESSION_SLOT':'2'}
    observations=[];applications=[];logs=[]
    connection=display.Display(env['DISPLAY']);root=connection.screen().root;atom=connection.intern_atom
    work=Path(env['HOME'])/'prefix-check';work.mkdir()
    protocols={'wlr-icons':Path(__file__).with_name('wlr-icons.xml'),
        'ext-icons':Path('/usr/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml')}
    for name,path in protocols.items():
        for mode,suffix in [('client-header','.h'),('private-code','.c')]:
            subprocess.run(['wayland-scanner',mode,str(path),str(work/(name+suffix))],check=True)
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','wayland-client'],text=True))
    foreign=work/'foreign-icons'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(work),
        str(Path(__file__).with_name('foreign-icons-probe.c')),*[str(work/(n+'.c')) for n in protocols],*flags,'-o',str(foreign)],check=True)

    def run(argv,label,environment=runenv,expected_code=0,timeout=150):
        result=subprocess.run(argv,env=environment,capture_output=True,text=True,timeout=timeout)
        (output/(label+'.stdout')).write_text(result.stdout)
        (output/(label+'.stderr')).write_text(result.stderr)
        assert result.returncode==expected_code,(label,result.returncode,result.stderr[-1000:])
        return result

    def start(argv,label,environment=runenv):
        log=(output/(label+'.log')).open('wb');logs.append(log)
        app=subprocess.Popen(argv,env=environment,stdout=log,stderr=subprocess.STDOUT);applications.append(app);return app

    def wait(test,label,seconds=30):
        deadline=time.monotonic()+seconds
        while True:
            result=test()
            if result:return result
            assert time.monotonic()<deadline,label
            time.sleep(.08)

    def main_window():
        for w in root.query_tree().children:
            try:
                if w.get_attributes().map_state==X.IsViewable and '7zfm.exe' in (w.get_wm_class() or ()):
                    kinds=w.get_full_property(atom('_NET_WM_WINDOW_TYPE'),Xatom.ATOM)
                    if kinds is not None and atom('_NET_WM_WINDOW_TYPE_NORMAL') in kinds.value:return w
            except error.XError:pass
        return None

    def close(w,app):
        w.send_event(event.ClientMessage(window=w,client_type=atom('WM_PROTOCOLS'),data=(32,[atom('WM_DELETE_WINDOW'),0,0,0,0])))
        connection.sync();assert app.wait(timeout=12)==0
        wait(lambda:main_window() is None,'Real window stayed mapped')

    def image(label):
        p=output/(label+'.png');subprocess.run(['grim',str(p)],env=env,check=True,timeout=5)
        return {'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}

    def identity(w,identifier):
        prop=w.get_full_property(atom('_NET_WM_NAME'),atom('UTF8_STRING'))
        title=prop.value.decode('utf-8') if prop is not None else w.get_wm_name()
        found={}
        for line in subprocess.check_output([str(foreign)],env=env,text=True,timeout=5).splitlines():
            protocol,name,value=line.split('\t')
            if name==title:found[protocol]=value
        assert found=={'wlr':'karyo-win32-'+identifier,'ext':'karyo-win32-'+identifier},found
        return found

    try:
        # This record/prefix was created by the old installed public package before dpkg upgrade.
        engine=home/'.local/share/karyo/windows/logs'
        prior_logs=set(engine.glob('*.log'))
        run(['/usr/bin/karyo-win32','--registered','existing-upgrade'],'existing-upgrade-launch')
        new_logs=set(engine.glob('*.log'))-prior_logs
        assert new_logs,'Upgraded launcher did not create a fresh engine log'
        assert all('KARYO_WIN32_QA=PASS' in path.read_text(errors='replace')
            and 'NoNewPrivs:\t1' in path.read_text(errors='replace') for path in new_logs)
        marker=home/'.local/share/karyo/windows/prefixes/existing-upgrade/upgrade-data.txt'
        assert marker.read_text()=='Existing user data: Пример\n'
        observations.append({'stage':'existing-flat-profile-after-package-upgrade','marker_sha256':hashlib.sha256(marker.read_bytes()).hexdigest()})
        installer=Path('/inputs/7z2606-x64.exe')
        donor=['python3','-I','-B','/actors/legacy-win32-v012.py','--app-id','legacy-source']
        run([*donor,str(installer),'/S'],'old-installer')
        context=json.loads(run(['python3','-I','-B','/actors/legacy-win32-v012.py','--inspect',
            '--app-id','legacy-source',str(installer)],'old-context').stdout)
        source=Path(context['prefix'])
        assert source==home/'.local/share/openkaryon/windows/prefixes/legacy-source/session-2'
        # Installer return precedes Wine's final registry flush. Establish a
        # complete stopped source before testing refusal of an active GUI.
        old.server_wait(source,runenv)
        assert (source/'system.reg').is_file()
        program=next((source/'drive_c').glob('Program Files*/7-Zip/7zFM.exe'))
        app=start([*donor,str(program)],'old-source-gui');window=wait(main_window,'Old source program did not map')
        busy=run(['/usr/bin/karyo-win32','--import-prefix',str(source),'--app-id','busy-copy'],
            'reject-active-source',expected_code=1,timeout=20)
        assert 'Закройте' in busy.stderr and app.poll() is None
        assert not (home/'.local/share/karyo/windows/prefixes/busy-copy').exists()
        close(window,app);old.server_wait(source,runenv)
        (source/'preserved-user-data.txt').write_text('Legacy data: Пример\n')
        before=old.snapshot(source)
        identifier='legacy-copy'
        preview=json.loads(run(['/usr/bin/karyo-win32','--inspect-import',str(source),'--app-id',identifier],
            'preview').stdout)
        destination=Path(preview['prefix'])
        assert preview['architecture']=='win64' and not destination.exists() and old.snapshot(source)==before
        imported=json.loads(run(['/usr/bin/karyo-win32','--import-prefix',str(source),'--app-id',identifier],
            'import').stdout)
        assert imported['status']=='imported' and not imported['architectureConverted']
        after=old.snapshot(destination)
        assert {p:r['sha256'] for p,r in before.items() if r['kind']=='file'}=={
            p:r['sha256'] for p,r in after.items() if r['kind']=='file'},'Imported file bytes changed'
        assert old.snapshot(source)==before,'Import changed the stopped legacy source'
        copied=destination/program.relative_to(source)
        inherited=json.loads(run(['/usr/bin/karyo-win32','--inspect',str(copied)],'new-context',
            environment={**runenv,'OPENKARYON_SESSION_SLOT':'1','HOME':'/not-the-account-home'}).stdout)
        assert inherited['applicationId']==identifier and inherited['prefix']==str(destination) and inherited['ownerUid']==1001
        shell=start(['/usr/bin/karyo-shell'],'shell')
        wait(lambda:'boolean true' in subprocess.run(['dbus-send','--session','--print-reply',
            '--dest=org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.NameHasOwner',
            'string:org.karyo.Shell'],env=env,capture_output=True,text=True,timeout=3).stdout,'Actual shell did not start')
        for attempt in range(2):
            args=['/usr/bin/karyo-win32',str(copied)] if attempt==0 else ['/usr/bin/karyo-win32','--registered',identifier]
            app=start(args,'imported-gui-'+str(attempt),{**runenv,'OPENKARYON_SESSION_SLOT':'1'})
            window=wait(main_window,'Imported real application did not map');time.sleep(.5)
            actual_id=identity(window,identifier)
            shot=image('imported-gui-'+str(attempt));close(window,app)
            observations.append({'stage':'imported-real-gui-'+str(attempt),'foreign':actual_id,**shot})
        edited=destination/'preserved-user-data.txt';edited.write_text('New profile edits are retained\n')
        run(['/usr/bin/karyo-win32','--import-prefix',str(source),'--app-id',identifier],
            'reject-repeat',expected_code=1)
        assert edited.read_text()=='New profile edits are retained\n' and old.snapshot(source)==before
        assert not list(destination.parent.glob('.import-*'))
        observations.append({'stage':'repeat-refused-preserving-edits-and-source',
            'source_snapshot_sha256':hashlib.sha256(json.dumps(before,sort_keys=True).encode()).hexdigest(),
            'import_content_sha256':imported['contentSha256'],'files':imported['files'],'bytes':imported['bytes'],
            'architecture':'win64','source_slot':'session-2','new_slot_environment_ignored':True})
        return observations
    finally:
        for app in reversed(applications):
            if app.poll() is None:
                app.terminate()
                try:app.wait(timeout=5)
                except subprocess.TimeoutExpired:app.kill();app.wait(timeout=3)
        for log in logs:log.close()
        diagnostics=output/'engine-logs';diagnostics.mkdir()
        for namespace in ('karyo','openkaryon'):
            for p in (home/('.local/share/'+namespace+'/windows/logs')).glob('*.log'):
                with p.open('rb') as stream:stream.seek(max(0,p.stat().st_size-131072));data=stream.read(131072)
                (diagnostics/(namespace+'-'+p.name)).write_bytes(data)
        connection.close()


if __name__=='__main__':
    module('caption_harness','caption-buttons.py').main(checker=checks,description=__doc__,
        actor_path=Path(__file__),private_pointer=False,real_wine_application=True)
