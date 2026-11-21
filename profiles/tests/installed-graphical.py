#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib,json,os,select,socket,subprocess,sys,time
from pathlib import Path
assert os.getuid()==0 and os.geteuid()==0 and socket.gethostname() in {"karyo-de-acceptance","karyo-de-ubuntu-acceptance"}
sys.dont_write_bytecode=True
sys.path.insert(0,"/usr/libexec/karyo/profiles")
from karyo_profiles.consoles import reserve_console
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import ResolverFiles,discover_dns
from karyo_profiles.graphical import GraphicalSession
from karyo_profiles.logind import Sessions
from karyo_profiles.model import DesktopSettings
from karyo_profiles.switching import ExistingSwitcher
from karyo_profiles.networks import NetworkCommand,create_network
from karyo_profiles.services import ResolverServices
from karyo_profiles.storage import ConfigStore,atomic_write,trusted_directory
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.user_manager import UserManagerScope,ManagerPending

def run(*args):return subprocess.check_output(args,text=True,timeout=15).strip()
def require(value,message):
 if not value:raise RuntimeError(message)
def processes(uid,name):
 found=[]
 for item in Path("/proc").iterdir():
  if not item.name.isdecimal():continue
  try:
   status=(item/"status").read_text()
   creds=next(r.split()[1:] for r in status.splitlines() if r.startswith("Uid:"))
   args=(item/"cmdline").read_bytes().split(bytes([0]))
   if str(uid) in creds and args and Path(os.fsdecode(args[0])).name==name:found.append(int(item.name))
  except (FileNotFoundError,ProcessLookupError):pass
 return sorted(found)
def wait(f,seconds=20):
 end=time.monotonic()+seconds
 while True:
  value=f()
  if value:return value
  require(time.monotonic()<end,"Graphical QA readiness timeout")
  time.sleep(.1)
def network_snapshot(command):
 return {key:command.run(*args) for key,args in {
 "links":("/usr/sbin/ip","-j","-d","link","show"),
 "routes":("/usr/sbin/ip","-j","-N","route","show","table","all"),
 "rules":("/usr/sbin/ip","-4","-j","-N","rule","show"),
 "nft":("/usr/sbin/nft","-j","list","ruleset"),
 "namespaces":("/usr/sbin/ip","netns","list")}.items()} | {
 "resolver":os.readlink("/etc/resolv.conf"),
 "resolver_sha":hashlib.sha256(Path("/etc/resolv.conf").read_bytes()).hexdigest(),
 "forward":Path("/proc/sys/net/ipv4/ip_forward").read_text()}
def status(phase):
 data={"phase":phase,"main_shell":original_shell,"namespace":None if network is None else network.name,
       "unit":None if graph is None else graph.name,"pid":None if graph is None else graph.pid}
 if graph is not None and graph.identity is not None:
  data["login"]=graph.identity.login.__dict__
  data["vt"]=graph.vt
 if child is not None:data["shells"]=processes(child.uid,"karyo-shell")
 print(json.dumps(data),flush=True)
 directory=trusted_directory("/var/lib/karyo-qa")
 try:atomic_write(directory,"graphical-current.json",(json.dumps(data)+chr(10)).encode())
 finally:os.close(directory)
 if phase in {"locked","wrong-confirmed","running","home","reentry-locked","reentry-running"}:
  require(select.select([0],[],[],20)[0],"QA visual driver acknowledgement timed out")
  packet=sys.stdin.buffer.readline(1025)
  require(len(packet)<=1024 and json.loads(packet)=={"ack":phase},"Unexpected QA driver acknowledgement")
require(not sys.argv[1:],"Fixed-host QA probe accepts no arguments")
request=sys.stdin.buffer.readline(1025)
require(len(request)<=1024 and request.endswith(bytes([10])),"Bounded QA request required")
request=json.loads(request)
require(set(request)=={"protection","password"} and request["protection"] in {"none","home","separate"},"Unsupported QA request")
protection=request["protection"];secret=request["password"]
require(isinstance(secret,str) and ((protection=="none" and not secret) or
 (protection=="separate" and len(secret)==24 and all(c in "0123456789abcdef" for c in secret)) or
 (protection=="home" and 8<=len(secret)<=128 and secret.isascii() and secret.isalnum())),"Unsupported ephemeral QA credential")
request.clear()
command=NetworkCommand();store=ConfigStore();accounts=LocalAccounts();sessions=Sessions()
original_hash=changed_hash=None;original_settings=None
def change_password(value,encrypted=False):
 args=["/usr/sbin/chpasswd","--encrypted"] if encrypted else ["/usr/sbin/chpasswd","--crypt-method","YESCRYPT"]
 result=subprocess.run(args,input=child.name+":"+value+chr(10),text=True,capture_output=True,timeout=5)
 require(result.returncode==0,"QA password update failed")
def restore_policy():
 global changed_hash
 if changed_hash is not None:
  require(accounts.password_hash(child.name)==changed_hash,"QA child hash changed externally; retain")
  change_password(original_hash,True);changed_hash=None
 if original_settings is not None and protection!="none":
  with store.locked():
   require(store.settings(child.name)==DesktopSettings(password=protection),"QA settings changed externally; retain")
   store.save_settings(child.name,original_settings)
owner=accounts.account("karyoqa");require(owner.uid==1001,"Unexpected QA owner")
source=next(row for row in sessions.all() if row.uid==owner.uid and row.active)
original_shell=processes(1001,"karyo-shell");require(len(original_shell)==1,"Main QA shell ambiguous")
original_network=network_snapshot(command)
baseline_pid=run("/usr/bin/systemctl","show","user@1001.service","-p","MainPID","--value")
original_bounding=next(row.split()[1] for row in Path("/proc/"+baseline_pid+"/status").read_text().splitlines() if row.startswith("CapBnd:"))
require(int(original_bounding,16)!=0,"QA baseline bounding set is empty")
original_accounts={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
 [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
prepared=False;child=network=files=services=manager=graph=None
try:
 print(run("/usr/local/libexec/karyo-qa-two-sessions","prepare"),flush=True);prepared=True
 child=accounts.account("kq-switch-work");require(child.uid==1002,"Unexpected QA child UID")
 if protection!="none":
  if protection=="separate":
   original_hash=accounts.password_hash(child.name)
   change_password(secret);changed_hash=accounts.password_hash(child.name)
  else:
   require(accounts.password_hash(child.name)==accounts.password_hash(owner.name),"Shared QA credentials were not synchronized")
  with store.locked():
   original_settings=store.settings(child.name)
   require(original_settings==DesktopSettings(),"Unexpected fresh QA settings")
   store.save_settings(child.name,DesktopSettings(password=protection))

 with store.locked():
  physical=discover_underlay(command);seed=discover_dns(physical,command)
  network=create_network(store,child.uid,command)
  files=ResolverFiles(network,seed).create()
  services=ResolverServices(files);services.start()
  manager=UserManagerScope(files);manager.prepare()
  with reserve_console(store) as console:
   graph=GraphicalSession(manager)
   graph.start(console)
 require(not processes(child.uid,"labwc") and not processes(child.uid,"karyo-shell"),"GUI launched before entry activation")
 require(sessions.get(source.id).active,"Source changed before explicit profile entry")
 status("waiting")
 with store.locked():print(json.dumps(graph.enter(owner)),flush=True)
 def ready():
  with store.locked():value=graph.compositor_ready()
  return value and len(processes(child.uid,"karyo-shell"))==1
 if protection!="none":
  wait(lambda:sessions.get(graph.identity.login.id).locked)
  require(not processes(child.uid,"karyo-shell") and not processes(child.uid,"karyo-files"),"Clients launched before initial authentication")
  status("locked")
  require(sessions.get(graph.identity.login.id).locked and not processes(child.uid,"karyo-shell")
          and not processes(child.uid,"karyo-files"),"Wrong password released initial clients")
  status("wrong-confirmed")
 wait(ready,35)
 with store.locked():graph.verify()
 for pid in [graph.pid,*processes(child.uid,"karyo-shell")]:
  rows=dict(row.split(":",1) for row in Path("/proc/"+str(pid)+"/status").read_text().splitlines() if ":" in row)
  require(all(int(rows[key].strip(),16)==0 for key in ("CapInh","CapPrm","CapEff","CapAmb")),"PAM capabilities reached GUI")
  require(rows["NoNewPrivs"].strip()=="0","GUI sudo was disabled")
  require(rows["CapBnd"].strip()==original_bounding,"GUI capability bounding set changed")
 status("running")
 child_shell=processes(child.uid,"karyo-shell")
 time.sleep(3)
 ExistingSwitcher(store).switch(child,owner.name)
 require(sessions.get(source.id).active,"Return to original Home failed")
 status("home")
 ExistingSwitcher(store).switch(owner,child.name)
 if protection!="none":
  require(sessions.get(graph.identity.login.id).locked,"Protected re-entry did not ask for authentication")
  status("reentry-locked")
  wait(lambda:not sessions.get(graph.identity.login.id).locked,35)
 require(processes(child.uid,"karyo-shell")==child_shell,"Graphical roundtrip restarted the shell")
 status("reentry-running")
 time.sleep(2)
finally:
 errors=[]
 try:
  current=sessions.get(source.id)
  require(current and (current.name,current.uid,current.vt)==(source.name,source.uid,source.vt),"Original QA source changed")
  sessions.activate(source.id)
 except Exception as error:errors.append("source restore: "+str(error))
 with store.locked():
  if graph is not None and not graph.closed:
   try:graph.close()
   except Exception as error:errors.append("graphical close: "+str(error))
  if manager is not None and not manager.closed:
   try:
    end=time.monotonic()+25
    while True:
     try:manager.close();break
     except ManagerPending:
      require(time.monotonic()<end,"Logind manager did not finish")
      time.sleep(.1)
   except Exception as error:errors.append("manager close: "+str(error))
  for resource in (services,files,network):
   if resource is not None and not resource.closed:
    try:
     require(not errors,"Retain network while graphical ownership is uncertain")
     resource.close()
    except Exception as error:errors.append("network close: "+str(error))
 try:restore_policy()
 except Exception as error:errors.append("QA policy restoration: "+str(error))
 if prepared and not errors:
  try:print(run("/usr/local/libexec/karyo-qa-two-sessions","cleanup"),flush=True)
  except Exception as error:errors.append("account cleanup: "+str(error))
 require(not errors,"; ".join(errors))
 require(processes(owner.uid,"karyo-shell")==original_shell,"Main shell PID changed")
 require(network_snapshot(command)==original_network,"Original network changed")
 require(all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==value for p,value in original_accounts.items()),"Original account/PAM bytes changed")
 require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-session","karyo-profile-password-sync"),"Package verification changed")
 require(not run("/usr/bin/dpkg","--audit"),"Package audit differs")
status("clean")
print("PASS native owned graphical profile and original host/account/PAM/GUI restoration",flush=True)
