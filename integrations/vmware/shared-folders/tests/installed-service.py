#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Installed HGFS lifecycle actor, restricted to the two disposable QA guests."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import stat
import subprocess

TARGETS = {'debian': 'karyo-de-acceptance', 'ubuntu': 'karyo-de-ubuntu-acceptance'}


def global_state():
 paths=[Path('/etc')/name for name in ('passwd','group','shadow','gshadow','fstab')]+[Path('/usr/bin/karyo-shell')]
 for directory in ('/etc/pam.d','/etc/lightdm'):
  paths.extend(path for path in Path(directory).rglob('*') if path.is_file())
 return {'files':{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},'display_manager':os.readlink('/etc/systemd/system/display-manager.service'),'root_vmtools':subprocess.check_output(['systemctl','show','open-vm-tools.service','-p','MainPID','-p','InvocationID','-p','ActiveState'],text=True)}


def install_disabled(spec):
    baseline=Path('/var/tmp')/(spec['share']+'-baseline.json')
    assert os.getuid()==0 and os.uname().nodename=={'debian':'karyo-de-acceptance','ubuntu':'karyo-de-ubuntu-acceptance'}[spec['base']]
    assert hashlib.sha256(Path(spec['package']).read_bytes()).hexdigest()==spec['package_sha256']
    with baseline.open('x') as stream:json.dump(global_state(),stream)
    baseline.chmod(0o600)
    initial=[line for line in Path('/proc/self/mountinfo').read_text().splitlines() if line.split()[4]=='/mnt/hgfs']
    assert len(initial)<=1 and (not initial or ' - fuse.vmhgfs-fuse ' in initial[0])
    print('INITIAL_VENDOR_MOUNT',initial,flush=True)
    subprocess.run(['dpkg','--install',spec['package']],check=True)
    print(subprocess.check_output(['systemctl','is-enabled','karyo-vmhgfs.service'],text=True),flush=True)
    print(subprocess.check_output(['systemctl','show','karyo-vmhgfs.service','-p','ActiveState','-p','Result','-p','ExecCondition'],text=True),flush=True)
    assert subprocess.check_output(['systemctl','is-enabled','karyo-vmhgfs.service'],text=True).strip()=='enabled'
    assert subprocess.check_output(['systemctl','show','karyo-vmhgfs.service','-p','ActiveState','--value'],text=True).strip()=='inactive'
    assert [line for line in Path('/proc/self/mountinfo').read_text().splitlines() if line.split()[4]=='/mnt/hgfs']==initial
    assert not subprocess.check_output(['dpkg','-V','karyo-vmware-shared-folders']).strip()
    print('PASS_DISABLED_HOST_SKIPPED',flush=True)


def service_native(spec):
    baseline=Path('/var/tmp')/(spec['share']+'-baseline.json')
    unit='karyo-vmhgfs.service';target=Path('/mnt/hgfs')
    def call(*args):return subprocess.check_output(list(args),text=True)
    def mount():
     rows=[line.split() for line in Path('/proc/self/mountinfo').read_text().splitlines() if line.split()[4]==str(target)]
     assert len(rows)<=1
     if not rows:return None
     row=rows[0];separator=row.index('-')
     identities=json.loads(call('/usr/bin/findmnt','--kernel=listmount','--list','--all','--nocanonicalize','--json','--output','ID,UNIQ-ID'))['filesystems']
     unique=next(int(item['uniq-id']) for item in identities if int(item['id'])==int(row[0]))
     return {'id':int(row[0]),'unique_id':unique,'device':row[2],'fstype':row[separator+1],'source':row[separator+2],'options':row[5]+','+row[separator+3]}
    subprocess.run(['systemd-analyze','verify','/usr/lib/systemd/system/'+unit],check=True)
    # VIX enableSharedFolders can create a separate vendor mount. First prove the
    # package leaves it alone; the QA controller then removes only that fixture
    # while this host exposes exclusively our newly generated dummy share.
    existing=mount()
    if existing:
     assert existing['fstype']=='fuse.vmhgfs-fuse' and existing['source']=='vmhgfs-fuse'
     assert call('/usr/bin/vmware-hgfsclient').splitlines()==[spec['share']]
     subprocess.run(['systemctl','start',unit],check=True)
     assert mount()==existing and not Path('/run/karyo-vmhgfs/ownership.json').exists()
     subprocess.run(['systemctl','stop',unit],check=True);assert mount()==existing
     subprocess.run(['umount','--no-canonicalize','--',str(target)],check=True)
     assert mount() is None
     print('PASS_VENDOR_MOUNT_LEFT_UNCHANGED_THEN_QA_FIXTURE_CLEANED',flush=True)

    subprocess.run(['systemctl','start',unit],check=True)
    assert call('systemctl','show',unit,'-p','ActiveState','--value').strip()=='active'
    first=mount();print(json.dumps(first),flush=True)
    assert first['fstype']=='fuse.vmhgfs-fuse' and 'nosuid' in first['options'] and 'nodev' in first['options']
    state=json.loads(Path('/run/karyo-vmhgfs/ownership.json').read_text());assert state['mount_id']==first['id'] and state['unique_id']==first['unique_id']
    assert call('systemctl','show',unit,'-p','NoNewPrivileges','--value').strip()=='yes'
    cgroup=call('systemctl','show',unit,'-p','ControlGroup','--value').strip()
    assert cgroup=='/system.slice/'+unit
    pids=[int(value) for value in (Path('/sys/fs/cgroup')/cgroup.lstrip('/')/'cgroup.procs').read_text().split()]
    worker=[]
    for pid in pids:
     status=Path('/proc')/str(pid)/'status'
     fields=dict(line.split(':',1) for line in status.read_text().splitlines() if ':' in line)
     if fields.get('Name','').strip()=='vmhgfs-fuse':
      assert set(fields['Uid'].split())=={'0'} and fields['NoNewPrivs'].strip()=='1'
      worker.append({'pid':pid,'uids':fields['Uid'].strip(),'no_new_privs':fields['NoNewPrivs'].strip(),'caps':fields['CapEff'].strip()})
    assert len(worker)==1
    print('NATIVE_OWNED_FUSE_WORKER',json.dumps(worker),flush=True)

    print('OWNER',state,flush=True)
    code="from pathlib import Path; p=Path('/mnt/hgfs')/"+repr(spec['share'])+"; assert (p/'host.txt').read_text()=='System HGFS: Привет!\\n'; (p/'guest.txt').write_text('Native service: Привет!\\n',encoding='utf-8')"
    subprocess.run(['runuser','-u','karyoqa','--','python3','-I','-c',code],check=True)
    subprocess.run(['unshare','--mount','--propagation','private','--','runuser','-u','karyoqa','--','python3','-I','-c',code],check=True)
    subprocess.run(['systemctl','start',unit],check=True);assert mount()['id']==first['id']
    subprocess.run(['systemctl','stop',unit],check=True);assert mount() is None
    # A real same-type replacement may reuse the old mountinfo/device numbers.
    subprocess.run(['systemctl','start',unit],check=True);original=mount()
    subprocess.run(['umount','--no-canonicalize','--',str(target)],check=True)
    subprocess.run(['/usr/bin/vmhgfs-fuse','.host:/',str(target),'-o','subtype=vmhgfs-fuse,allow_other,nosuid,nodev'],check=True)
    replacement=mount();assert replacement['unique_id']!=original['unique_id']
    try:
     subprocess.run(['systemctl','stop',unit],check=True)
     assert mount()==replacement
     assert (target/spec['share']/'host.txt').read_text()=='System HGFS: Привет!\n'
     print('PASS_REPLACEMENT',json.dumps({'original':original,'replacement':replacement}),flush=True)
    finally:
     assert mount()==replacement;subprocess.run(['umount','--no-canonicalize','--',str(target)],check=True)
    # A different administrator mount must survive service start/stop.
    subprocess.run(['mount','-t','tmpfs','-o','size=64k,nosuid,nodev','tmpfs',str(target)],check=True)
    foreign=mount()
    try:
     sentinel=target/'karyo-foreign-keep';sentinel.write_text('preserve foreign mount')
     subprocess.run(['systemctl','start',unit],check=True)
     assert mount()['id']==foreign['id'] and not Path('/run/karyo-vmhgfs/ownership.json').exists()
     subprocess.run(['systemctl','stop',unit],check=True)
     assert mount()['id']==foreign['id'] and sentinel.read_text()=='preserve foreign mount'
    finally:
     assert mount()['id']==foreign['id'];subprocess.run(['umount',str(target)],check=True)
    # Admin disable must survive reinstall/upgrade metadata handling.
    subprocess.run(['systemctl','disable',unit],check=True)
    subprocess.run(['dpkg','--install',spec['package']],check=True)
    print('AFTER_REINSTALL',subprocess.run(['systemctl','is-enabled',unit],capture_output=True,text=True).stdout.strip(),flush=True)
    assert subprocess.run(['systemctl','is-enabled',unit],capture_output=True,text=True).stdout.strip()=='disabled'
    assert call('systemctl','show',unit,'-p','ActiveState','--value').strip()=='inactive'
    subprocess.run(['systemctl','enable',unit],check=True);subprocess.run(['systemctl','start',unit],check=True)
    assert mount()['fstype']=='fuse.vmhgfs-fuse'
    assert global_state()==json.loads(baseline.read_text())
    print('READY_DISCONNECTED_PURGE',flush=True)


def purge_disconnected(spec):
    baseline=Path('/var/tmp')/(spec['share']+'-baseline.json')
    target=Path('/mnt/hgfs');unit='karyo-vmhgfs.service'
    rows=[line for line in Path('/proc/self/mountinfo').read_text().splitlines() if line.split()[4]==str(target)]
    assert len(rows)==1 and ' - fuse.vmhgfs-fuse ' in rows[0]
    subprocess.run(['dpkg','--purge','karyo-vmware-shared-folders'],check=True)
    assert not [line for line in Path('/proc/self/mountinfo').read_text().splitlines() if line.split()[4]==str(target)]
    assert target.is_dir() and not Path('/usr/lib/systemd/system/'+unit).exists()
    assert not Path('/usr/libexec/karyo/vmware-shared-folders').exists()
    assert not Path('/run/karyo-vmhgfs').exists()
    assert global_state()==json.loads(baseline.read_text())
    print('PASS_DISCONNECTED_PURGE_GLOBAL_STATE_PRESERVED',flush=True)


def cleanup_stop(spec):
    baseline=Path('/var/tmp')/(spec['share']+'-baseline.json')
    if Path('/usr/lib/systemd/system/karyo-vmhgfs.service').exists(): subprocess.run(['systemctl','stop','karyo-vmhgfs.service'],check=False)


def main():
    phases = {'install-disabled': install_disabled, 'service-native': service_native,
              'purge-disconnected': purge_disconnected, 'cleanup-stop': cleanup_stop}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spec', required=True)
    parser.add_argument('--phase', choices=phases, required=True)
    args = parser.parse_args()
    spec = json.loads(Path(args.spec).read_text())
    if (not isinstance(spec, dict) or set(spec) != {'base','share','package','package_sha256','source'}
            or spec.get('base') not in TARGETS or not all(isinstance(v,str) for v in spec.values())
            or not re.fullmatch(r'karyo-hgfs-service-[0-9a-f]{10}',spec['share'])
            or not re.fullmatch(r'[0-9a-f]{64}',spec['package_sha256'])
            or not re.fullmatch(r'[0-9a-f]{40}',spec['source'])
            or spec['package'] != '/home/qaadmin/'+spec['share']+'.deb'):
        raise ValueError('Invalid isolated QA specification')
    if os.getuid()!=0 or os.uname().nodename!=TARGETS[spec['base']] or pwd.getpwnam('karyoqa').pw_uid!=1001:
        raise ValueError('This actor requires the named disposable kernel QA guest')
    package=Path(spec['package']);info=package.lstat()
    if (not stat.S_ISREG(info.st_mode) or info.st_uid!=1000 or info.st_nlink!=1
            or hashlib.sha256(package.read_bytes()).hexdigest()!=spec['package_sha256']):
        raise ValueError('Changed or foreign QA package')
    os.environ['PATH']='/usr/sbin:/usr/bin:/sbin:/bin'
    phases[args.phase](spec)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
