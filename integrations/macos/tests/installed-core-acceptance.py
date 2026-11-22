#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Installed CLI acceptance with an explicit Linux engine fixture, not Darling."""
import hashlib
import json
import os
from pathlib import Path
import pwd
import stat
import subprocess
import sys


def run(argv,environment,code):
    result=subprocess.run(argv,env=environment,capture_output=True,text=True,timeout=15)
    assert result.returncode==code,(argv,result.returncode,result.stderr)
    return result


def main():
    assert os.getuid()==1001 and os.geteuid()==1001
    account=pwd.getpwuid(1001);home=Path(account.pw_dir)
    assert home==Path('/home/win32qa') and not (home/'.karyo-macos').exists()
    output=Path('/output');binary=Path('/usr/share/karyo-qa/macos-inspection-probe')
    version=subprocess.check_output(['dpkg-query','-W','-f=${Version}','karyo-macos-core'],text=True).strip()
    assert binary.is_file() and binary.stat().st_size==32
    env={**os.environ,'HOME':'/not-account-home','USER':'root','LOGNAME':'root',
         'PYTHONPATH':'/not-the-runtime','PYTHONHOME':'/not-the-runtime',
         'OPENKARYON_SESSION_SLOT':'2','QT_QPA_PLATFORM':'wayland',
         'QML_IMPORT_PATH':'/linux-only','DARLING_SHARE_X11_IPC':'1'}
    for name in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','XAUTHORITY'):
        env.pop(name,None)
    # Isolated shebang must ignore Python environment injection. Use the installed
    # public entry, not a checkout import or a hand-written interpreter command.
    status=json.loads(run(['/usr/bin/karyo-macos','--status','--json'],env,1).stdout)
    assert not status['installed'] and status['experimental']
    preview=json.loads(run(['/usr/bin/karyo-macos','--inspect',str(binary)],env,0).stdout)
    assert preview['ownerUid']==1001 and preview['persistent'] and not preview['prefixCreated']
    prefix=Path(preview['prefix']);assert prefix.is_relative_to(home/'.karyo-macos')
    missing=run(['/usr/bin/karyo-macos',str(binary)],env,1)
    assert 'Darling не установлен' in missing.stderr and not (home/'.karyo-macos').exists()
    if version=='0.1.0-2':
        for argv in (['/usr/bin/karyo-macos','--gui'],['/usr/bin/karyo-macos']):
            frontend=run(argv,env,1)
            assert 'необязательный пакет karyo-macos' in frontend.stderr
        assert not (home/'.karyo-macos').exists()
    env['OPENKARYON_SESSION_SLOT']='1'
    again=json.loads(run(['/usr/bin/karyo-macos','--inspect',str(binary)],env,0).stdout)
    assert again['prefix']==preview['prefix']
    bad=home/'not-mach-o';bad.write_bytes(b'MZ'+b'\0'*30);bad.chmod(0o755)
    rejected=run(['/usr/bin/karyo-macos','--inspect',str(bad)],env,1)
    assert 'Mach-O' in rejected.stderr and not (home/'.karyo-macos').exists()
    # This intentionally named fixture is a Linux subprocess. Its results prove
    # the installed wrapper contract only, never Mach-O/GUI engine compatibility.
    engine_dir=home/'fixture-bin';engine_dir.mkdir(mode=0o700)
    engine=engine_dir/'darling'
    engine.write_text('#!/usr/bin/python3 -I\nimport json,os,sys\nfrom pathlib import Path\n'
        'r={"fixtureOnly":True,"argv":sys.argv[1:],"uid":os.getuid(),"euid":os.geteuid(),\n'
        '"home":os.environ.get("HOME"),"prefix":os.environ.get("DPREFIX"),\n'
        '"environment":{k:v for k,v in os.environ.items() if k.startswith(("QT_","QML","QSG_")) or k in ("DARLING_SHARE_X11_IPC",)},\n'
        '"status":Path("/proc/self/status").read_text()}\n'
        'Path(os.environ["QA_REPORT"]).write_text(json.dumps(r))\n'
        'os.write(1,b"fixture diagnostic\\n")\nsys.exit(23)\n')
    engine.chmod(0o700)
    report=home/'engine.json';env.update(PATH=str(engine_dir)+':/usr/bin:/bin',QA_REPORT=str(report))
    args=["literal '$()' spaces",'; touch '+str(home/'must-not-exist'),'one\ntwo','--flag']
    result=run(['/usr/bin/karyo-macos',str(binary),'--',*args],env,23)
    actual=json.loads(report.read_text());assert actual['fixtureOnly']
    assert actual['argv']==['exec',str(binary),*args] and actual['uid']==actual['euid']==1001
    assert actual['home']==str(home) and actual['prefix']==str(prefix) and not actual['environment']
    fields=dict(line.split(':',1) for line in actual['status'].splitlines() if ':' in line)
    assert all(int(fields[key],16)==0 for key in ('CapInh','CapPrm','CapEff','CapAmb'))
    assert fields['NoNewPrivs'].strip()=='1'
    assert not prefix.exists() and not (home/'must-not-exist').exists()
    assert stat.S_IMODE(prefix.parent.stat().st_mode)==0o700
    logs=list((prefix.parent/'logs').glob('*.log'));assert len(logs)==1
    assert b'Karyo exit code: 23' in logs[0].read_bytes() and stat.S_IMODE(logs[0].stat().st_mode)==0o600
    prefix.mkdir();marker=prefix/'saved-data';marker.write_text('Пример existing data\n')
    before=marker.read_bytes();inode=marker.stat().st_ino
    run(['/usr/bin/karyo-macos',str(binary),'--',*args],env,23)
    assert marker.read_bytes()==before and marker.stat().st_ino==inode
    assert len(list((prefix.parent/'logs').glob('*.log')))==2
    accepted={'accepted':True,'fixture_only':True,'darling_runtime_proven':False,
        'uid':1001,'public_entry':'/usr/bin/karyo-macos','prefix':str(prefix),
        'package_version':version,
        'checks':['status-without-engine','noncreating-inspect','account-and-slot-authority',
            'missing-engine-no-state','foreign-format-refusal','isolated-python-entry',
            'literal-arguments-status23','clean-environment-capabilities','private-exclusive-logs','existing-data-inode-preserved'],
        'marker_sha256':hashlib.sha256(before).hexdigest()}
    if version=='0.1.0-2':accepted['checks'].extend(['explicit-gui-without-frontend','default-gui-without-frontend'])
    (output/'native-acceptance.json').write_text(json.dumps(accepted,indent=2)+'\n')
    print(json.dumps(accepted),flush=True)


if __name__=='__main__':main()
