#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in installed user-manager scope check on two disposable QA hosts."""
import ctypes,hashlib,json,os,secrets,socket,stat,subprocess,sys,time,uuid
from pathlib import Path
assert os.getuid()==0 and os.geteuid()==0 and socket.gethostname() in {"karyo-de-acceptance","karyo-de-ubuntu-acceptance"}
sys.dont_write_bytecode=True
sys.path.insert(0,"/usr/libexec/karyo/profiles")
from karyo_profiles.consoles import reserve_console
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import ResolverFiles,discover_dns
from karyo_profiles.logind import Sessions
from karyo_profiles.networks import NetworkCommand,create_network
from karyo_profiles.services import ResolverServices
from karyo_profiles.storage import ConfigStore
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.user_manager import UserManagerScope,ManagerPending

def run(*args):
 return subprocess.check_output(args,text=True,timeout=15).strip()
def require(value,message):
 if not value:raise RuntimeError(message)
def info(unit):
 return dict(row.split("=",1) for row in run("/usr/bin/systemctl","show",unit,"--no-pager",
  "-p","MainPID","-p","InvocationID","-p","ControlGroup","-p","ActiveState","-p","SubState",
  "-p","DropInPaths","-p","FragmentPath","-p","NetworkNamespacePath").splitlines())
def wait(f,seconds=10):
 end=time.monotonic()+seconds
 while True:
  value=f()
  if value:return value
  require(time.monotonic()<end,"Native QA readiness timeout")
  time.sleep(.05)
def process_session(pid):
 lib=ctypes.CDLL("libsystemd.so.0");lib.sd_pid_get_session.argtypes=(ctypes.c_int,ctypes.POINTER(ctypes.c_void_p));lib.sd_pid_get_session.restype=ctypes.c_int
 libc=ctypes.CDLL(None);libc.free.argtypes=(ctypes.c_void_p,)
 value=ctypes.c_void_p();code=lib.sd_pid_get_session(pid,ctypes.byref(value))
 if code<0:return None
 try:return ctypes.string_at(value).decode("ascii")
 finally:libc.free(value)
def root_file(path,data):
 require(not path.exists() and not path.is_symlink(),"QA file collision")
 fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o644)
 try:os.write(fd,data);os.fsync(fd);st=os.fstat(fd);return (st.st_dev,st.st_ino,data)
 finally:os.close(fd)
def remove_file(path,record):
 st=path.lstat();require((st.st_dev,st.st_ino)==record[:2] and st.st_uid==0 and stat.S_ISREG(st.st_mode) and path.read_bytes()==record[2],"QA file changed; retain")
 path.unlink()
def namespace(pid):
 st=os.stat("/proc/"+str(pid)+"/ns/net");return st.st_dev,st.st_ino

def network_snapshot(command):
 return {key:command.run(*args) for key,args in {
  "links":("/usr/sbin/ip","-j","-d","link","show"),
  "routes":("/usr/sbin/ip","-j","-N","route","show","table","all"),
  "rules":("/usr/sbin/ip","-4","-j","-N","rule","show"),
  "nft":("/usr/sbin/nft","-j","list","ruleset"),
  "namespaces":("/usr/sbin/ip","netns","list")}.items()} | {
  "resolver_target":os.readlink("/etc/resolv.conf"),
  "resolver_sha":hashlib.sha256(Path("/etc/resolv.conf").read_bytes()).hexdigest(),
  "forward":Path("/proc/sys/net/ipv4/ip_forward").read_text()}
def password_update(account,value,encrypted=False):
 args=["/usr/sbin/chpasswd","--encrypted"] if encrypted else ["/usr/sbin/chpasswd","--crypt-method","YESCRYPT"]
 result=subprocess.run(args,input=account.name+":"+value+chr(10),text=True,capture_output=True,timeout=5)
 require(result.returncode==0,"QA child password update failed")
def restore_password():
 global changed_hash,temporary_password
 if changed_hash is not None:
  require(LocalAccounts().password_hash(account.name)==changed_hash,"QA child password changed externally; retain")
  password_update(account,original_hash,True)
  changed_hash=None;temporary_password=""
command=NetworkCommand();store=ConfigStore()
original_network=network_snapshot(command)
baseline_manager=run("/usr/bin/systemctl","show","user@1001.service","-p","MainPID","--value")
original_bounding=next(row.split()[1] for row in Path("/proc/"+baseline_manager+"/status").read_text().splitlines() if row.startswith("CapBnd:"))
require(int(original_bounding,16)!=0,"QA baseline has an empty bounding set")
require(not sys.argv[1:],"This fixed-host probe accepts no arguments")
changed_hash=original_hash=None;temporary_password=""
main=run("/usr/bin/pgrep","-u","1001","-x","karyo-shell")
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
network=files=services=scope=None
pam=None;pam_record=None
unit=None;unit_record=None;manager=None;manager_record=None;session=None;session_record=None
prepared=False;evidence={}
try:
 print(run("/usr/local/libexec/karyo-qa-two-sessions","prepare"),flush=True);prepared=True
 account=LocalAccounts().account("kq-switch-work")
 require(account.uid==1002,"Unexpected QA UID")
 manager="user@1002.service"
 initial=info(manager)
 require(initial["MainPID"]=="0" and initial["ActiveState"]=="inactive" and not initial["ControlGroup"] and not initial["InvocationID"],"QA user manager already running")
 require(not Path("/run/user/1002").exists(),"QA runtime already exists")
 require(not Path("/var/lib/systemd/linger/kq-switch-work").exists(),"QA user lingers")
 with store.locked():
  physical=discover_underlay(command);seed=discover_dns(physical,command)
  network=create_network(store,account.uid,command)
  files=ResolverFiles(network,seed).create()
  services=ResolverServices(files);services.start()
  scope=UserManagerScope(files);scope.prepare()
  token=uuid.uuid4().hex;service_name="karyo-qa-session-"+token
  pam=Path("/etc/pam.d")/service_name
  pam_record=root_file(pam,b"auth requisite pam_deny.so\n@include common-account\n@include common-session\n")
  with reserve_console(store) as console:
   console.verify();unit=service_name+".service"
   properties=("Type=exec","Restart=no","KillMode=control-group","TimeoutStopSec=5s",
     "User=1002","Group="+str(account.gid),"PAMName="+service_name,"TTYPath="+console.path,
     "StandardInput=tty-fail","StandardOutput=journal","StandardError=journal","TTYReset=no","TTYVHangup=no","TTYVTDisallocate=no",
     "WorkingDirectory="+account.home,*files.plan.client_properties())
   args=["/usr/bin/systemd-run","--quiet","--collect","--unit="+unit]
   args+=["--property="+v for v in properties]
   args+=["--setenv="+v for v in ("XDG_SESSION_TYPE=wayland","XDG_SESSION_CLASS=user","XDG_SEAT=seat0",
      "XDG_VTNR="+str(console.vt),"HOME="+account.home,"USER="+account.name,"LOGNAME="+account.name,"PATH=/usr/local/bin:/usr/bin:/bin")]
   run(*args,"--","/usr/bin/sleep","180")
   unit_record=info(unit);pid=int(unit_record["MainPID"])
   require(pid>1 and len(unit_record["InvocationID"])==32,"QA session unit capture failed")
   session=wait(lambda:process_session(pid))
   session_record=dict(row.split("=",1) for row in run("/usr/bin/loginctl","show-session",session,"--all").splitlines())
   value=Sessions().get(session)
   require(value and value.uid==1002 and value.name==account.name and value.vt==console.vt,"Native logind identity differs")
   require(not value.active,"Native PAM session unexpectedly activated its console")
   manager_record=info(manager);mpid=int(manager_record["MainPID"])
   require(mpid>1 and len(manager_record["InvocationID"])==32,"QA user manager capture failed")
   require(namespace(pid)==network.identity and namespace(mpid)==network.identity,"Session/manager network scope differs")
   owners=Sessions().owners(account.uid)
   login=next(owner for owner in owners if owner.id==session)
   scope.capture(login);scope.verify()

   # Ephemeral test credential: memory and stdin only, never output or files.
   original_hash=LocalAccounts().password_hash(account.name)
   temporary_password=secrets.token_urlsafe(32)
   password_update(account,temporary_password)
   changed_hash=LocalAccounts().password_hash(account.name)
   require(changed_hash!=original_hash,"QA password update did not change the child")
   child_code = """import os,json,subprocess,sys
from pathlib import Path
secret=sys.stdin.readline().rstrip(chr(10))
st=os.stat('/run/systemd/resolve')
elevate=subprocess.run(['/usr/bin/sudo','-S','-k','-p','','/usr/bin/id','-u'],input=secret+chr(10),capture_output=True,text=True,timeout=4)
secret=""
bus=subprocess.run(['/usr/bin/busctl','--system','--timeout=2s','call','org.freedesktop.login1','/org/freedesktop/login1','org.freedesktop.DBus.Peer','Ping'],capture_output=True,timeout=3)
caps=dict(line.split(':',1) for line in Path('/proc/self/status').read_text().splitlines() if line.startswith(('CapInh:','CapPrm:','CapEff:','CapAmb:','CapBnd:','NoNewPrivs:')))
print(json.dumps({'pid':os.getpid(),'uid':os.getuid(),'network':os.readlink('/proc/self/ns/net'),'resolver_directory':[st.st_dev,st.st_ino],'sudo':elevate.returncode==0 and elevate.stdout.strip()=='0','host_bus':bus.returncode==0,'caps':{k:v.strip() for k,v in caps.items()}}))
"""
   args=("/usr/sbin/runuser","-u",account.name,"--","/usr/bin/env","XDG_RUNTIME_DIR=/run/user/1002",
      "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus","/usr/bin/systemd-run","--user","--quiet","--wait","--pipe",
      "/usr/bin/python3","-I","-c",child_code)
   child=json.loads(subprocess.check_output(args,input=temporary_password+chr(10),text=True,timeout=15))
   restore_password()
   private=os.stat("/run/"+files.plan.runtime_name)
   require(child["resolver_directory"]==[private.st_dev,private.st_ino],"Activated child lost private DNS mount")
   require(child["uid"]==1002 and child["sudo"] and child["host_bus"] and child["caps"]["NoNewPrivs"]=="0","Activated child sudo/bus policy changed")
   print("Activated child diagnostics:",json.dumps(child),flush=True)
   require(all(child["caps"][key]=="0000000000000000" for key in ("CapInh","CapPrm","CapEff","CapAmb")),"Activated child inherited unexpected capabilities")
   require(child["caps"]["CapBnd"]==original_bounding,"Activated child capability bounding set changed")
   scope.verify()
   evidence={"unit":unit_record,"logind":session_record,"process_cgroup":Path("/proc/"+str(pid)+"/cgroup").read_text(),
      "manager":manager_record,"manager_cgroup":Path("/proc/"+str(mpid)+"/cgroup").read_text(),
      "network":network.identity,"user_bus_activation":child,"vt":console.vt,
      "session_caps":[r for r in Path("/proc/"+str(pid)+"/status").read_text().splitlines() if r.startswith(("Cap","NoNewPrivs"))]}
   print(json.dumps(evidence,indent=2),flush=True)
finally:
 errors=[]
 try:restore_password()
 except Exception as e:errors.append("password restoration: "+str(e))
 if session_record:
  try:
   current=dict(row.split("=",1) for row in run("/usr/bin/loginctl","show-session",session,"--all").splitlines())
   require(all(current.get(k)==session_record.get(k) for k in ("Id","Name","User","VTNr","Leader","Scope","Service")),"QA logind identity changed; retain")
   run("/usr/bin/loginctl","terminate-session",session)
  except Exception as e:errors.append("session: "+str(e))
 if unit_record:
  try:
   current=info(unit)
   if current["ActiveState"]!="inactive":
    require(current["InvocationID"]==unit_record["InvocationID"] and current["MainPID"] in {"0",unit_record["MainPID"]},"QA unit changed; retain")
    run("/usr/bin/systemctl","stop",unit)
  except Exception as e:errors.append("unit: "+str(e))
 if scope is not None:
  try:
   with store.locked():
    release_started=time.monotonic()
    end=release_started+25
    while True:
     try:
      scope.close();print("Native logind manager release seconds:",round(time.monotonic()-release_started,3),flush=True);break
     except ManagerPending:
      require(time.monotonic()<end,"Native manager did not finish its logind lifetime")
      time.sleep(.1)
  except Exception as e:errors.append("manager scope: "+str(e))
 if pam_record:
  try:
   require(not errors,"Keep PAM until known processes stopped")
   remove_file(pam,pam_record)
  except Exception as e:errors.append("PAM: "+str(e))
 with store.locked():
  for resource in (services,files,network):
   if resource is not None and not resource.closed:
    try:
     require(not errors,"Retain network while uncertain")
     resource.close()
    except Exception as e:errors.append("network: "+str(e))
 if prepared and not errors:
  try:print(run("/usr/local/libexec/karyo-qa-two-sessions","cleanup"),flush=True)
  except Exception as e:errors.append("account: "+str(e))
 require(not errors,"; ".join(errors))
 require(all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==sha for p,sha in before.items()),"Original account/PAM bytes differ")
 require(run("/usr/bin/pgrep","-u","1001","-x","karyo-shell")==main,"Main shell PID changed")
 require(network_snapshot(command)==original_network,"Original network snapshot changed")
 require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-password-sync"),"Package verification differs")
 require(not run("/usr/bin/dpkg","--audit"),"Package audit differs")
 require(not Path("/run/user/1002").exists(),"Native child runtime still exists")
print("PASS installed UserManagerScope, inactive native PAM session and exact account/PAM/GUI restoration")
