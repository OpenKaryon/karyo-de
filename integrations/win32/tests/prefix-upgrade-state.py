#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic QA account seed/snapshot; never installed as a product helper."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import shutil
import stat
import subprocess


def home():
    account=pwd.getpwnam('win32qa')
    assert account.pw_uid==1001 and account.pw_dir=='/home/win32qa'
    assert Path('/usr/share/karyo-qa/runtime-probe.exe').is_file()
    return Path(account.pw_dir)


def snapshot(folder):
    rows={}
    for parent,dirs,files in os.walk(folder,followlinks=False):
        for name in ['.']+dirs+files:
            path=Path(parent) if name=='.' else Path(parent)/name
            key=path.relative_to(folder).as_posix()
            info=path.lstat()
            row={'mode':stat.S_IMODE(info.st_mode),'mtime_ns':info.st_mtime_ns,'uid':info.st_uid,'gid':info.st_gid}
            if stat.S_ISLNK(info.st_mode):row.update(kind='link',target=os.readlink(path))
            elif stat.S_ISDIR(info.st_mode):row['kind']='directory'
            else:
                assert stat.S_ISREG(info.st_mode)
                row.update(kind='file',sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            rows[key]=row
    return rows


def server_wait(prefix,environment):
    server=shutil.which('wineserver')
    if server is None:
        query=subprocess.run(['dpkg-query','-L','wine64','libwine'],capture_output=True,text=True,timeout=10)
        candidates=sorted({p for p in query.stdout.splitlines() if Path(p).name=='wineserver'
            and Path(p).is_file() and os.access(p,os.X_OK)})
        assert len(candidates)==1;server=candidates[0]
    subprocess.run([server,'-w'],env={**environment,'WINEPREFIX':str(prefix)},check=True,timeout=45)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed',action='store_true')
    parser.add_argument('--snapshot',type=Path)
    parser.add_argument('--verify',type=Path)
    args=parser.parse_args();folder=home()
    if args.seed:
        assert os.getuid()==1001 and not (folder/'.local').exists()
        folder.chmod(0o700)
        env={**os.environ,'WINEDEBUG':'-all','WINEDLLOVERRIDES':'mscoree,mshtml=d'}
        for key in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','XAUTHORITY','LABWC_PID'):
            env.pop(key,None)
        status=subprocess.check_output(['dpkg-query','-W','-f=${Version}','karyo-win32'],text=True).strip()
        assert status=='0.1.0-1','Seed must use the older installed launcher package'
        with (folder/'prefix-upgrade-seed.log').open('wb') as log:
            subprocess.run(['/usr/bin/karyo-win32','--app-id','existing-upgrade',
                '/usr/share/karyo-qa/runtime-probe.exe'],env=env,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=150)
        prefix=folder/'.local/share/karyo/windows/prefixes/existing-upgrade'
        server_wait(prefix,env)
        marker=prefix/'upgrade-data.txt';marker.write_text('Existing user data: Пример\n')
        logs='\n'.join(p.read_text(errors='replace') for p in (folder/'.local/share/karyo/windows/logs').glob('*.log'))
        assert 'KARYO_WIN32_QA=PASS' in logs and 'NoNewPrivs:\t1' in logs
        return
    assert os.getuid()==0,'Build-time verification only; not a production privileged API'
    data=snapshot(folder)
    if args.snapshot:args.snapshot.write_text(json.dumps(data,indent=2)+'\n')
    elif args.verify:
        assert data==json.loads(args.verify.read_text()),'Package upgrade changed existing user data'
        assert subprocess.check_output(['dpkg-query','-W','-f=${Version}','karyo-win32'],text=True).strip()=='0.1.0-2'
    else:parser.error('Choose seed, snapshot or verify')


if __name__=='__main__':main()
