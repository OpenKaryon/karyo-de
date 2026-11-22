#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host real enrollment, journal reattach and connected close gate."""
import hashlib,json,os,shutil,socket,stat,subprocess,sys,time,uuid
from pathlib import Path
sys.dont_write_bytecode=True;sys.path.insert(0,'/usr/libexec/karyo/profiles')
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import discover_dns
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.journal_netbird import JournalNetBird
from karyo_profiles.journal_session import JournalSession
from karyo_profiles.logind import Sessions
from karyo_profiles.storage import ConfigStore
from karyo_profiles.switching import ExistingSwitcher
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.user_manager import ManagerPending
assert os.getuid()==os.geteuid()==0 and socket.gethostname() in {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'} and not sys.argv[1:]
base='debian' if socket.gethostname()=='karyo-de-acceptance' else 'ubuntu'
root=Path('/var/lib/karyo-qa-netbird-connected');assert not os.path.lexists(root)
assert not os.path.lexists('/usr/bin/netbird') and not os.path.lexists('/var/lib/karyo/profile-vpn/1002')
assert Path('/proc/sys/net/ipv4/ip_forward').read_text().strip()=='0'
store,accounts,sessions=ConfigStore(),LocalAccounts(),Sessions()
source=next(s for s in sessions.all() if s.uid==1001 and s.name=='karyoqa' and s.active)
def run(*args):return subprocess.check_output(args,text=True,timeout=20).strip()
assert not run('/usr/sbin/ip','netns','list')
with RuntimeJournal() as journal:assert not journal.snapshot()['profiles']
os.mkdir(root,0o700)
package=Path('/home/qaadmin/netbird-client-input/netbird_0.82.1_linux_amd64.deb')
assert hashlib.sha256(package.read_bytes()).hexdigest()=='4adff2bbc1529942e914c181badfc17585c78f0532ccaf6c000f5c9300d9a632'
subprocess.run(['/usr/bin/dpkg-deb','-x',str(package),str(root/'input')],check=True)
binary=root/'input/usr/bin/netbird';binary_sha=hashlib.sha256(binary.read_bytes()).hexdigest()
subprocess.run(['/usr/bin/install','-o','root','-g','root','-m','755',str(binary),'/usr/bin/netbird'],check=True)
original_conf={str(p):p.read_text() for p in Path('/proc/sys/net/ipv4/conf').glob('*/*')}
original_accounts={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path('/etc/passwd'),Path('/etc/shadow'),Path('/etc/group'),Path('/etc/gshadow'),*Path('/etc/pam.d').glob('common-*')]}
run('/usr/local/libexec/karyo-qa-two-sessions','prepare')
child=accounts.account('kq-switch-work');assert child.uid==1002
admission=Path('/var/lib/karyo-qa-netbird-admission/setup-key');info=admission.lstat()
assert stat.S_ISREG(info.st_mode) and info.st_uid==info.st_gid==0 and info.st_mode&0o777==0o600 and info.st_nlink==1 and info.st_size<65
key=admission.read_bytes();uuid.UUID(key.decode().strip())
keyfile=Path(child.home)/'.karyo-qa-setup-key';fd=os.open(keyfile,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
try:os.fchown(fd,1002,1002);os.write(fd,key);os.fsync(fd)
finally:os.close(fd)
key_inode=(keyfile.stat().st_dev,keyfile.stat().st_ino)
def user(*args):return ['/usr/sbin/runuser','-u',child.name,'--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/1002','DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus',*args]
operations={}
def cli(operation,*args):
 number=operations.get(operation,0);operations[operation]=number+1
 name='karyo-qa-observe-'+operation+'-'+str(number)+'.service'
 result=subprocess.run(user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect','--unit='+name,'--','/usr/bin/karyo-netbird-cli',operation,*args),capture_output=True,text=True,timeout=65)
 fd=os.open(root/(operation+'-'+str(number)+'.log'),os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
 try:os.write(fd,(result.stdout+result.stderr).encode())
 finally:os.close(fd)
 return result

def host_snapshot():
 return {key:run(*args) for key,args in {
 'routes':('/usr/sbin/ip','-4','-j','-N','route','show','table','all'),
 'rules':('/usr/sbin/ip','-4','-j','-N','rule','show'),
 'nft':('/usr/sbin/nft','-j','list','ruleset')}.items()}|{'resolver_sha':hashlib.sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest(),'resolver':os.readlink('/etc/resolv.conf')}

def kernel(network):
 return {name:json.loads(network._ip(*args,inside=True)) for name,args in {
  'links':('-j','-d','link','show'),'addresses':('-j','address','show'),
  'routes':('-4','-j','-N','route','show','table','all'),'rules':('-4','-j','-N','rule','show')}.items()}|{
  'nft':json.loads(network.command.run('/usr/bin/nsenter','--net='+network.path,'--','/usr/sbin/nft','-j','list','ruleset'))}

original_host=host_snapshot()
Path('/proc/sys/net/ipv4/ip_forward').write_text('1\n')
with RuntimeJournal() as journal:
 with store.locked():
  network=JournalNetwork.start(journal,store,child,source);native=network.network
  physical=discover_underlay(native.command);forwarding=JournalForwarding.start(journal,network,physical)
  dns=JournalDNS.start(journal,network,discover_dns(physical,native.command))
  provider=JournalNetBird.start(journal,dns);state_identity=provider.scope.state.identity
  graph=JournalSession.start(journal,dns);provider.release_handles();graph.enter(accounts.account('karyoqa'))
  before=kernel(native)
 result=cli('up','--management-url','http://192.168.230.163:18443','--setup-key-file',str(keyfile),'--hostname','karyo-qa-'+base)
 with store.locked():
  graph.provider.scope.unit.verify();connected=kernel(native)
  try:native.verify();verifies=True
  except RuntimeError:verifies=False
 status=cli('status','--json');value=json.loads(status.stdout) if status.returncode==0 else {}
 summary={'up_exit':result.returncode,'status_exit':status.returncode,'daemon_status':value.get('daemonStatus'),
  'status_fields':sorted(value),'existing_network_verifier_accepts_connected':verifies}
 assert result.returncode==0 and status.returncode==0 and value.get('daemonStatus')=='Connected' and verifies
 with store.locked():graph.verify_consumers();graph.provider.verify()
 record=journal.record(graph.token)
 assert journal.snapshot()['schema']==7 and record['network']['contents']=='tunnel' and record['session']['catalog']=='netbird'
 identities={'provider':record['provider']['service'],'graphical':record['session']['graphical'],'manager':record['session']['manager']}
 graph.release_handles();journal.close()
 journal=RuntimeJournal()
 with store.locked():
  graph=JournalSession.restore(journal,store,record)
  graph.verify_consumers();graph.provider.verify()
  restored=journal.record(graph.token)
  assert restored['provider']['service']==identities['provider'] and restored['session']['graphical']==identities['graphical'] and restored['session']['manager']==identities['manager']
 dns,forwarding,network=graph.dns,graph.forwarding,graph.network
 native=network.network
 status2=cli('status','--json');assert status2.returncode==0 and json.loads(status2.stdout).get('daemonStatus')=='Connected'
 report={'base':base,'summary':summary,'before':before,'connected':connected,'fresh_journal_reattach_without_restart':True,'explicit_down_called':False}
 print(json.dumps({'base':base,'connected':True,'fresh_journal_reattach_without_restart':True,'explicit_down_called':False}),flush=True)
 ExistingSwitcher(store).switch(child,'karyoqa')
 deadline=time.monotonic()+35
 while True:
  try:
   with store.locked():graph.close()
   break
  except ManagerPending:
   assert time.monotonic()<deadline;time.sleep(.1)
 with store.locked():graph.provider.close();dns.close();forwarding.close();network.close()
 graph.release_handles();assert not journal.snapshot()['profiles'];journal.close()
Path('/proc/sys/net/ipv4/ip_forward').write_text('0\n')
for name,data in original_conf.items():
 if Path(name).read_text()!=data:Path(name).write_text(data)
assert (keyfile.stat().st_dev,keyfile.stat().st_ino)==key_inode and keyfile.read_bytes()==key;keyfile.unlink()
state=Path('/var/lib/karyo/profile-vpn/1002/netbird');assert (state.stat().st_dev,state.stat().st_ino)==state_identity
assert json.loads((state/'owner.json').read_text())['account']['name']==child.name
shutil.rmtree(state);state.parent.rmdir()
run('/usr/local/libexec/karyo-qa-two-sessions','cleanup')
assert {name:hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in original_accounts}==original_accounts
assert sessions.get(source.id)==source and not run('/usr/sbin/ip','netns','list')
assert hashlib.sha256(Path('/usr/bin/netbird').read_bytes()).hexdigest()==binary_sha;Path('/usr/bin/netbird').unlink()
assert host_snapshot()==original_host
assert not run('/usr/bin/dpkg','-V','karyo-profile-accounts','karyo-profile-host','karyo-profile-runtime','karyo-profile-netbird-client') and not run('/usr/bin/dpkg','--audit')
fd=os.open('/var/log/karyo-qa/netbird-connected-kernel.json',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
try:os.write(fd,(json.dumps(report,indent=2)+chr(10)).encode())
finally:os.close(fd)
shutil.rmtree(root)  # Previously absent disposable fixture, after its consumers/units/namespace exited.
print('PASS real connected provider, fresh journal reattachment and connected-owner teardown without down; host state restored',flush=True)
