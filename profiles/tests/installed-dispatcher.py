#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Root-only fixed-host QA for the optional installed host transport."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

assert os.getuid()==0 and os.geteuid()==0 and not sys.argv[1:]
assert socket.gethostname() in {"karyo-de-acceptance","karyo-de-ubuntu-acceptance"}
sys.dont_write_bytecode=True
sys.path.insert(0,"/usr/libexec/karyo/profiles")
from karyo_profiles.dispatcher import ADDRESS,request,socket_node
from karyo_profiles.logind import Sessions
from karyo_profiles.networks import NetworkCommand,create_network
from karyo_profiles.storage import ConfigStore


def run(*args):return subprocess.check_output(args,text=True,timeout=15).strip()
def require(value,message):
    if not value:raise RuntimeError(message)
def info():
    return {line.split("=",1)[0]:line.split("=",1)[1] for line in run("/usr/bin/systemctl","show",
        "karyo-profile-host.service","-p","MainPID","-p","InvocationID","-p","ActiveState").splitlines()}
def snapshot():
    command=NetworkCommand()
    return {key:command.run(*args) for key,args in {
        "links":("/usr/sbin/ip","-j","-d","link","show"),
        "routes":("/usr/sbin/ip","-j","-N","route","show","table","all"),
        "rules":("/usr/sbin/ip","-4","-j","-N","rule","show"),
        "nft":("/usr/sbin/nft","-j","list","ruleset"),
        "namespaces":("/usr/sbin/ip","netns","list")}.items()}|{
        "resolver":os.readlink("/etc/resolv.conf"),
        "resolver_sha":hashlib.sha256(Path("/etc/resolv.conf").read_bytes()).hexdigest(),
        "forward":Path("/proc/sys/net/ipv4/ip_forward").read_text()}

sessions=Sessions();store=ConfigStore()
source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
owner=next(row for row in sessions.owners(1001) if row.id==source.id)
require(owner.scope=="session-"+source.id+".scope","Unexpected native source scope")
cgroup=Path("/sys/fs/cgroup/user.slice/user-1001.slice")/owner.scope
cg_info=cgroup.stat();cg_identity=(cg_info.st_dev,cg_info.st_ino)
original_network=snapshot()
original_accounts={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
    [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
require(not os.path.lexists(ADDRESS),"Existing host socket must not be adopted")
for name in ("karyo-profile-host.service","karyo-profile-host.socket"):
    require(run("/usr/bin/systemctl","show",name,"-p","ActiveState","--value")=="inactive","Existing active host unit")
require(not Path("/etc/systemd/system/sockets.target.wants/karyo-profile-host.socket").exists(),"Host socket already enabled")
network=None;unit=None;node=None

# Each QA child moves ONLY ITS OWN PID into the captured existing login scope.
# This tests native peer/cgroup identity, not an interactive pkexec prompt.
client_code="""import ctypes,json,os,socket,sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,"/usr/libexec/karyo/profiles")
from karyo_profiles.dispatcher import ADDRESS,request,send,receive
from karyo_profiles.logind import Sessions,process_session
sid,mode,cgroup,device,inode,fd=sys.argv[1:]
source=Sessions().get(sid)
assert source and source.active and source.uid==1001 and source.name=="karyoqa"
info=Path(cgroup).stat();assert (info.st_dev,info.st_ino)==(int(device),int(inode))
Path(cgroup,"cgroup.procs").write_text(str(os.getpid()))
assert process_session()==sid
before=os.stat("/proc/self/ns/net")
if int(fd)>=0:
    libc=ctypes.CDLL(None,use_errno=True)
    assert libc.setns(int(fd),0x40000000)==0
    after=os.stat("/proc/self/ns/net")
    assert (before.st_dev,before.st_ino)!=(after.st_dev,after.st_ino)
    before_mount=os.stat("/proc/self/ns/mnt")
    assert libc.unshare(0x00020000)==0
    after_mount=os.stat("/proc/self/ns/mnt")
    assert (before_mount.st_dev,before_mount.st_ino)!=(after_mount.st_dev,after_mount.st_ino)
os.environ.update(PKEXEC_UID="65534",XDG_SESSION_ID="forged",USER="root")
if mode=="invalid":
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
        connection.settimeout(2);connection.connect(ADDRESS)
        send(connection,{"version":1,"operation":"list","owner":"root"},4096,2)
        assert receive(connection,65536,10)=={"version":1,"error":"invalid"}
    print(json.dumps({"mode":mode,"unknown_fields":"rejected"}))
else:
    rows=request({"version":1,"operation":"list"})
    assert rows==[{"user":"karyoqa","label":"Karyo QA","running":True,"active":True,"vt":8,"current":True}],rows
    result=request({"version":1,"operation":"switch","name":"karyoqa"})
    assert result=={"user":"karyoqa","vt":8,"changed":False},result
    print(json.dumps({"mode":mode,"caller":"karyoqa","forwarded_environment":"ignored","private_net":int(fd)>=0,"private_mount":int(fd)>=0,"switch":"same-session"}))
"""

def client(mode,fd=-1):
    result=subprocess.run(["/usr/bin/python3","-I","-c",client_code,source.id,mode,str(cgroup),
        str(cg_identity[0]),str(cg_identity[1]),str(fd)],pass_fds=() if fd<0 else (fd,),
        text=True,capture_output=True,timeout=45)
    require(result.returncode==0,"Native host peer QA failed: "+result.stderr)
    print(result.stdout.strip(),flush=True)

try:
    run("/usr/bin/systemctl","start","karyo-profile-host.socket")
    node=socket_node()
    require(info()["ActiveState"]=="inactive","Socket alone unexpectedly starts service")
    # This ordinary process cannot open the root-only filesystem socket.
    denied=subprocess.run(["/usr/bin/setpriv","--reuid=1001","--regid=1001","--clear-groups",
        "/usr/bin/python3","-I","-c",
        "import socket; s=socket.socket(socket.AF_UNIX); s.connect('/run/karyo/profiles.sock')"],
        text=True,capture_output=True,timeout=5)
    require(denied.returncode!=0 and "PermissionError" in denied.stderr,"Unprivileged socket access was not denied")
    require(info()["ActiveState"]=="inactive","Unprivileged denial activated service")
    # A root connection triggers activation; capture this fresh native unit
    # before exercising any operation, including failure paths.
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as trigger:
        trigger.settimeout(2);trigger.connect(ADDRESS)
        deadline=time.monotonic()+8
        while True:
            candidate=info()
            if candidate["ActiveState"]=="active" and int(candidate["MainPID"])>1 and len(candidate["InvocationID"])==32:
                unit=candidate;break
            require(time.monotonic()<deadline,"Host socket activation did not settle")
            time.sleep(.05)
    client("host")
    pid=int(unit["MainPID"])
    for kind in ("user","net","mnt"):
        left,right=os.stat("/proc/"+str(pid)+"/ns/"+kind),os.stat("/proc/1/ns/"+kind)
        require((left.st_dev,left.st_ino)==(right.st_dev,right.st_ino),"Controller left host namespaces")
    # Root SSH/system-service processes cannot impersonate an active GUI peer.
    try:request({"version":1,"operation":"list"})
    except (PermissionError,ConnectionResetError,BrokenPipeError):pass
    else:raise RuntimeError("Remote root process was accepted as a GUI caller")
    client("invalid")
    require(not sessions.owners(1002),"QA namespace UID has an existing login")
    with store.locked():network=create_network(store,1002)
    client("private",network.fd)
    require(info()==unit,"Host service unexpectedly restarted")
    print(json.dumps({"host_pid":pid,"socket_activation":True,"ordinary_denied":True,"remote_root_denied":True,"host_namespace":True}),flush=True)
finally:
    if network is not None:
        with store.locked():network.close()
    if unit is not None:
        require(info()==unit,"Host unit identity changed; retain for review")
        run("/usr/bin/systemctl","stop","karyo-profile-host.service")
    if node is not None:
        require(info()["ActiveState"]=="inactive","Uncaptured host service remains; retain socket")
        require(socket_node()==node,"Host socket inode changed; retain for review")
        run("/usr/bin/systemctl","stop","karyo-profile-host.socket")

require(not os.path.lexists(ADDRESS),"Host socket was not removed")
require(snapshot()==original_network,"Network state changed after native host QA")
require({path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}==original_accounts,"Accounts or common PAM changed")
require(sessions.get(source.id)==source,"Main graphical login changed")
require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-host","karyo-profile-session","karyo-profile-password-sync"),"Package verification failed")
require(not run("/usr/bin/dpkg","--audit"),"Package audit failed")
print("PASS native host/private-network peer identity, bounded protocol and exact restoration",flush=True)
