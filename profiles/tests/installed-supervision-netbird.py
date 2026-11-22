#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host provider, graphical owner and host restart acceptance."""
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import socket
import stat
import uuid
import subprocess
import sys
import time

if (os.getuid()!=0 or os.geteuid()!=0 or socket.gethostname() not in
        {"karyo-de-acceptance","karyo-de-ubuntu-acceptance"} or sys.argv[1:] not in ([],["--connected"],["--controller"],["--client"])):
    raise SystemExit("Use only the fixed disposable hosts as root")
sys.dont_write_bytecode=True
sys.path.insert(0,"/usr/libexec/karyo/profiles")
from karyo_profiles.dispatcher import ADDRESS as SOCKET_ADDRESS,socket_node
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.journal import RuntimeJournal,JournalRetained
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.journal_netbird import JournalNetBird
from karyo_profiles.dns import discover_dns
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.logind import Sessions
from karyo_profiles.networks import NetworkCommand
from karyo_profiles.storage import ConfigStore


def run(*args):return subprocess.check_output(args,text=True,timeout=15).strip()
def require(value,message):
    if not value:raise RuntimeError(message)
def snapshot():
    command=NetworkCommand()
    return {key:command.run(*args) for key,args in {
        "links":("/usr/sbin/ip","-j","-d","link","show"),
        "routes":("/usr/sbin/ip","-j","-N","route","show","table","all"),
        "rules":("/usr/sbin/ip","-4","-j","-N","rule","show"),
        "nft":("/usr/sbin/nft","-j","list","ruleset"),
        "netns":("/usr/sbin/ip","netns","list")}.items()}|{
        "resolver":os.readlink("/etc/resolv.conf"),
        "resolver_sha":hashlib.sha256(Path("/etc/resolv.conf").read_bytes()).hexdigest()}


from karyo_profiles.journal_session import JournalSession,SessionRegistry,manager_receipt
from karyo_profiles.user_manager import UserManagerScope,uid_processes
from karyo_profiles.switching import ExistingSwitcher
from karyo_profiles.journal_session_record import validate_session
from karyo_profiles.model import DesktopSettings


def processes(uid,name):
    found=[]
    for path in Path("/proc").iterdir():
        if not path.name.isdecimal():continue
        try:
            status=(path/"status").read_text().splitlines()
            values=next(line.split()[1:] for line in status if line.startswith("Uid:"))
            args=(path/"cmdline").read_bytes().split(bytes([0]))
            if str(uid) in values and args and Path(os.fsdecode(args[0])).name==name:found.append(int(path.name))
        except (FileNotFoundError,ProcessLookupError):pass
    return sorted(found)


def ui(record):
    graph=record["session"]["graphical"]
    current=Sessions().get(graph["login"]["id"])
    require(current is not None and current.uid==1002 and current.vt==graph["vt"],"Captured GUI login changed")
    return {"manager":record["session"]["manager"]["live"]["pid"],"compositor":graph["pid"],
        "shell":processes(1002,"karyo-shell"),"lock":processes(1002,"karyo-lock"),
        "editor":processes(1002,"karyo-editor"),
        "active":current.active,"locked":current.locked,"vt":current.vt}



def controller():
    request=json.loads(sys.stdin.buffer.readline(1025));require(set(request)<={"mode","connected"} and request.get("mode") in {"none","home","prepared","closed"},"Unknown session QA mode")
    connected=request.get("connected",False);require(type(connected) is bool,"Invalid enrollment flag")
    mode=request["mode"];store=ConfigStore();accounts=LocalAccounts();sessions=Sessions()
    child=accounts.account("kq-switch-work");require(child.uid==1002,"Unexpected QA child")
    source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"],"Prior session journal exists")
        with store.locked():
            network=JournalNetwork.start(journal,store,child,source)
            physical=discover_underlay(network.network.command)
            JournalForwarding.start(journal,network,physical)
            dns=JournalDNS.start(journal,network,discover_dns(physical,network.network.command))
            provider=JournalNetBird.start(journal,dns)
            if mode=="prepared":
                before=journal.record(network.token)
                journal.change(before,dict(before,session={"pending":"manager-prepare","manager":None,"graphical":None}))
                manager=UserManagerScope(dns.files,netbird=True);manager.prepare()
                before=journal.record(network.token)
                journal.change(before,dict(before,session={"pending":None,"manager":manager_receipt(manager),"graphical":None}))
                print(json.dumps({"ready":mode,"pid":os.getpid(),"record":journal.record(network.token)}),flush=True)
                sys.stdin.buffer.read(1)
                raise RuntimeError("This prepared controller requires captured termination")
            scope=JournalSession.start(journal,dns)
            provider.release_handles()  # The complete session now owns its observers.
            scope.enter(accounts.account("karyoqa"))
        deadline=time.monotonic()+20
        while True:
            record=journal.record(scope.token);view=ui(record)
            with store.locked():ready=scope.graph.compositor_ready()
            if ready and view["active"] and ((mode in {"none","closed"} and len(view["shell"])==1) or
                    (mode=="home" and view["locked"] and not view["shell"] and len(view["lock"])==1)):break
            require(time.monotonic()<deadline,"Initial owned desktop readiness timed out")
            time.sleep(.1)
        with store.locked():scope.verify_consumers()
        endpoint=("KARYO_NETBIRD_ENDPOINT="+str(scope.provider.scope.endpoint)).encode()
        for pid in (scope.manager.identity.pid,scope.graph.pid,*view["shell"],*view["lock"]):
            require(endpoint in Path("/proc",str(pid),"environ").read_bytes().split(bytes([0])),"Captured owner lost derived provider endpoint")
        if connected:
            require(mode!="prepared","Offline preparation has no ordinary enrollment client")
            result=subprocess.run(["/usr/sbin/runuser","-u",child.name,"--","/usr/bin/env",
                "XDG_RUNTIME_DIR=/run/user/1002","DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus",
                "/usr/bin/systemd-run","--user","--wait","--pipe","--collect",
                "--unit=karyo-qa-recovery-enroll-"+scope.token+".service","--","/usr/bin/karyo-netbird-cli","up",
                "--management-url","http://192.168.230.163:18443","--setup-key-file",
                str(Path(child.home)/".karyo-qa-recovery-setup-key"),"--hostname","karyo-recovery-"+socket.gethostname()],
                capture_output=True,text=True,timeout=65)
            require(result.returncode==0,"Disposable connected enrollment failed")
            with store.locked():scope.verify_consumers();scope.provider.verify()
        result=run("/usr/sbin/runuser","-u",child.name,"--","/usr/bin/env",
            "XDG_RUNTIME_DIR=/run/user/1002","DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus",
            "/usr/bin/systemd-run","--user","--wait","--pipe","--collect",
            "--unit=karyo-qa-netbird-autoenv.service","--","/usr/bin/karyo-netbird-cli","status","--json")
        require(json.loads(result).get("daemonStatus")==("Connected" if connected else "NeedsLogin"),"Ordinary CLI did not inherit the private provider endpoint")
        if mode=="closed":
            ExistingSwitcher(store).switch(child,"karyoqa")
            with store.locked():
                scope.update(pending="login-close");scope.graph.close()
                scope.update(pending="manager-close",graphical=None)
            deadline=time.monotonic()+30
            while not scope.manager.inactive(scope.manager.info()) or sessions.owners(child.uid) or uid_processes(child.uid):
                require(time.monotonic()<deadline,"Captured manager did not naturally exit")
                time.sleep(.1)
            record=journal.record(scope.token)
        print(json.dumps({"ready":mode,"pid":os.getpid(),"record":record,"ui":view,"connected":connected}),flush=True)
        sys.stdin.buffer.read(1)
        raise RuntimeError("This controller must be terminated by its fixed driver")


def client():
    import ctypes
    request=json.loads(sys.stdin.buffer.readline(16385));record=request["record"]
    account=LocalAccounts().account("kq-switch-work");require(account.uid==1002,"Unexpected client account")
    validate_session(record["session"],account)
    graph=record["session"]["graphical"];login=graph["login"]
    scope="session-"+login["id"]+".scope"
    require(run("/usr/bin/systemctl","show",scope,"-p","InvocationID","--value")==graph["scope_invocation"],"Client login scope changed")
    cgroup=Path("/sys/fs/cgroup/user.slice/user-1002.slice")/scope
    Path(cgroup,"cgroup.procs").write_text(str(os.getpid()))
    libc=ctypes.CDLL(None,use_errno=True)
    for kind,flag in (("net",0x40000000),("mnt",0x00020000)):
        fd=os.open("/proc/"+str(graph["pid"])+"/ns/"+kind,os.O_RDONLY|os.O_CLOEXEC)
        try:require(libc.setns(fd,flag)==0,"Could not enter captured QA desktop context")
        finally:os.close(fd)
    from karyo_profiles.dispatcher import request as contact
    rows=contact({"version":1,"operation":"list"})
    require(any(r["user"]==account.name and r["active"] and r["current"] for r in rows),"Host did not derive the active Work caller")
    print(json.dumps({"family_listing_from_live_work":True}),flush=True)


def unit_info():
    return {line.split("=",1)[0]:line.split("=",1)[1] for line in run("/usr/bin/systemctl","show",
        "karyo-profile-host.service","-p","InvocationID","-p","MainPID","-p","ActiveState").splitlines()}



def end_compositor(record,crash):
    graph=record["session"]["graphical"]
    if crash:
        import signal
        from karyo_profiles.journal_session import stat_start
        fd=os.pidfd_open(graph["pid"])
        try:
            executable=os.stat("/proc/"+str(graph["pid"])+"/exe")
            require([executable.st_dev,executable.st_ino]==graph["compositor"] and stat_start(graph["pid"])==graph["start"],"Captured compositor process changed")
            require(run("/usr/bin/systemctl","show",graph["login"]["scope"],"-p","InvocationID","--value")==graph["scope_invocation"],"Captured compositor scope changed")
            signal.pidfd_send_signal(fd,signal.SIGKILL)
        finally:os.close(fd)
    else:
        run("/usr/sbin/runuser","-u","kq-switch-work","--","/usr/bin/env",
            "XDG_RUNTIME_DIR=/run/user/1002","DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus",
            "/usr/bin/systemd-run","--user","--wait","--pipe","--collect",
            "--unit=karyo-qa-logout-"+record["token"]+".service","--","/usr/bin/labwc","--exit")


def supervise(record,source,mode,connected=False):
    require(unit_info()["ActiveState"]=="inactive","A host controller is already active")
    run("/usr/bin/systemctl","start","karyo-profile-host.socket");node=socket_node();unit=None
    try:
        result=subprocess.run(["/usr/bin/python3","-I",__file__,"--client"],input=json.dumps({"record":record}),text=True,capture_output=True,timeout=35)
        require(result.returncode==0,"Live host request failed: "+result.stderr)
        unit=unit_info();require(unit["ActiveState"]=="active" and int(unit["MainPID"])>1,"Native host registry is not active")
        if connected:
            # Read-only inspection: the real socket host owns the journal lock.
            captured=json.loads(Path("/run/karyo/profile-host/state.json").read_bytes())
            require(captured["schema"]==7 and captured["profiles"]==[record] and record["session"].get("catalog")=="netbird","Connected host recovery changed captured native receipts")
            status=run("/usr/sbin/runuser","-u","kq-switch-work","--","/usr/bin/env",
                "XDG_RUNTIME_DIR=/run/user/1002","DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus",
                "/usr/bin/systemd-run","--user","--wait","--pipe","--collect",
                "--unit=karyo-qa-recovery-status-"+record["token"]+".service","--","/usr/bin/karyo-netbird-cli","status","--json")
            require(json.loads(status).get("daemonStatus")=="Connected","Provider disconnected during actual host recovery")
            from karyo_profiles.journal_session import stat_start
            require(stat_start(record["provider"]["service"]["unit"]["pid"])==record["provider"]["service"]["unit"]["start"],"Recovered provider PID/start changed")
            print(json.dumps({"mode":mode,"connected_after_controller_sigkill":True,"same_native_receipts":True,
                "host_pid":int(unit["MainPID"]),"provider_pid":record["provider"]["service"]["unit"]["pid"]}),flush=True)
        if mode=="background":
            current=Sessions().get(source.id)
            require(current is not None and (current.uid,current.name,current.vt)==(source.uid,source.name,source.vt),"Return Home identity changed")
            Sessions().activate(source.id)
        end_compositor(record,mode!="logout")
        state=Path("/run/karyo/profile-host/state.json");deadline=time.monotonic()+30
        while True:
            require(unit_info()==unit,"Supervising host identity changed")
            info=state.stat()
            require(info.st_uid==0 and info.st_nlink==1 and info.st_mode&0o777==0o600 and info.st_size<=1024*1024,"Unsafe supervised journal")
            if not json.loads(state.read_bytes())["profiles"]:break
            require(time.monotonic()<deadline,"Installed host did not finish native logout teardown")
            time.sleep(.1)
        current=Sessions().get(source.id)
        require(current==source,"The original Home login was not restored")
        from karyo_profiles.consoles import KernelConsoles
        with KernelConsoles() as kernel:require(kernel.state().active==source.vt,"Kernel did not return to Home")
        messages=run("/usr/bin/journalctl","--no-pager","-o","cat","_SYSTEMD_INVOCATION_ID="+unit["InvocationID"])
        events=[line for line in messages.splitlines() if line.startswith("karyo-profile-host: exited desktop return ")]
        require(len(events)==1,"Expected exactly one native return decision")
        if mode=="background":require(events[0].endswith("seat-unchanged"),"Host tried to activate over the already active Home")
        print(json.dumps({"mode":mode,"host_pid":int(unit["MainPID"]),"event":events[0],"home":source.id,"vt":source.vt,"cleanup":"complete"}),flush=True)
    finally:
        if unit is not None:
            require(unit_info()==unit,"Captured supervision host changed")
            run("/usr/bin/systemctl","stop","karyo-profile-host.service")
        require(unit_info()["ActiveState"]=="inactive","Uncaptured host controller retained")
        require(socket_node()==node,"Captured host socket changed")
        run("/usr/bin/systemctl","stop","karyo-profile-host.socket")


if sys.argv[1:]==["--controller"]:controller();raise SystemExit(0)
if sys.argv[1:]==["--client"]:client();raise SystemExit(0)

connected=sys.argv[1:]==["--connected"]
store=ConfigStore();sessions=Sessions();accounts=LocalAccounts()
source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
main_shell=processes(1001,"karyo-shell");require(len(main_shell)==1,"Original main shell is ambiguous")
require(Path("/proc/sys/net/ipv4/ip_forward").read_text().strip()=="0","Changed forwarding baseline")
require(not Path("/var/lib/karyo/profile-vpn/1002").exists(),"Prior provider state requires review")
original=snapshot()
original_conf={str(p):p.read_text() for p in Path("/proc/sys/net/ipv4/conf").glob("*/*")}
state_identity=None
original_accounts={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Prior resources need review")
run("/usr/local/libexec/karyo-qa-two-sessions","prepare")
child=accounts.account("kq-switch-work");original_policy=store.settings(child.name)
keyfile=key_identity=None
if connected:
    admission=Path("/var/lib/karyo-qa-netbird-admission/setup-key");value=admission.lstat()
    require(stat.S_ISREG(value.st_mode) and value.st_uid==value.st_gid==0 and value.st_mode&0o777==0o600 and value.st_nlink==1 and value.st_size<65,"Unsafe lab admission key")
    key=admission.read_bytes();uuid.UUID(key.decode().strip())
    keyfile=Path(child.home)/".karyo-qa-recovery-setup-key"
    fd=os.open(keyfile,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:os.fchown(fd,1002,1002);os.write(fd,key);os.fsync(fd)
    finally:os.close(fd)
    key_identity=(keyfile.stat().st_dev,keyfile.stat().st_ino)
try:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("1\n")
    for mode in (("logout","crash","background","locked-crash","closed") if connected else
                 ("prepared","logout","crash","background","locked-crash","closed")):
        policy=DesktopSettings(password="home" if mode=="locked-crash" else "none")
        with store.locked():
            require(store.settings(child.name)==original_policy,"QA policy changed")
            store.save_settings(child.name,policy)
        with subprocess.Popen(["/usr/bin/python3","-I",__file__,"--controller"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as process:
            requested=mode if mode in {"prepared","closed"} else policy.password
            process.stdin.write((json.dumps({"mode":requested,"connected":connected})+chr(10)).encode());process.stdin.flush()
            try:
                require(select.select([process.stdout],[],[],110)[0],"Observed desktop controller readiness timeout")
                packet=process.stdout.readline(32769)
                if not packet:raise RuntimeError("Observed controller failed: "+process.stderr.read(32768).decode())
                ready=json.loads(packet);require(ready["pid"]==process.pid and ready["ready"]==requested,"Captured controller changed")
                record=ready["record"]
                current=tuple(record["provider"]["state"]["directory"])
                if state_identity is None:state_identity=current
                else:require(state_identity==current,"Provider state generation changed between sessions")
                process.kill();require(process.wait(timeout=5)==-9,"Captured controller did not terminate")
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
        if mode in {"prepared","closed"}:
            with RuntimeJournal() as journal:
                require(journal.snapshot()["profiles"]==[record],"Interrupted session receipt changed")
                registry=SessionRegistry(journal,store).restore()
                require(not registry.profiles and not journal.snapshot()["profiles"],"Interrupted provider/session cleanup incomplete")
                registry.release_handles()
            print(json.dumps({"mode":mode,"cleanup":"complete","home":source.id}),flush=True)
        else:supervise(record,source,mode,connected)
        with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Supervisor left ownership records")
        with store.locked():
            require(store.settings(child.name)==policy,"QA policy changed before restoration")
            store.save_settings(child.name,original_policy)
        require(snapshot()==original and processes(1001,"karyo-shell")==main_shell,"Original network/main GUI changed")
finally:
    with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Retained resources; do not remove QA account")
    require(store.settings(child.name)==original_policy,"QA policy not restored")
    if state_identity is not None:
        state=Path("/var/lib/karyo/profile-vpn/1002/netbird");info=state.stat()
        require((info.st_dev,info.st_ino)==state_identity,"Provider QA state was replaced")
        require(json.loads((state/"owner.json").read_text())["account"]["name"]==child.name,"Provider ownership stamp changed")
        shutil.rmtree(state);state.parent.rmdir()  # Explicit previously absent fixture only.
    if keyfile is not None:
        require((keyfile.stat().st_dev,keyfile.stat().st_ino)==key_identity and keyfile.read_bytes()==key,"Captured lab key changed")
        keyfile.unlink()
    run("/usr/local/libexec/karyo-qa-two-sessions","cleanup")
    Path("/proc/sys/net/ipv4/ip_forward").write_text("0\n")
    for name,value in original_conf.items():
        if Path(name).read_text()!=value:Path(name).write_text(value)

require({path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}==original_accounts,"Account/common PAM restoration failed")
require(snapshot()==original and sessions.get(source.id)==source and processes(1001,"karyo-shell")==main_shell,"Final original source changed")
require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-host","karyo-profile-session","karyo-profile-runtime","karyo-profile-netbird-client","karyo-profile-password-sync"),"Package verification failed")
require(not run("/usr/bin/dpkg","--audit"),"Package audit failed")
print("PASS installed provider/graphical reattachment, inherited clients, logout/crash and Home return; connected="+str(connected),flush=True)
