#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host live GUI crash and observer reattachment acceptance."""
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import time

if (os.getuid()!=0 or os.geteuid()!=0 or socket.gethostname() not in
        {"karyo-de-acceptance","karyo-de-ubuntu-acceptance"} or sys.argv[1:] not in ([],["--controller"],["--client"])):
    raise SystemExit("Use only the fixed disposable hosts as root")
sys.dont_write_bytecode=True
sys.path.insert(0,"/usr/libexec/karyo/profiles")
from karyo_profiles.dispatcher import ADDRESS as SOCKET_ADDRESS,socket_node
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.journal import RuntimeJournal,JournalRetained
from karyo_profiles.journal_network import JournalNetwork,restore_network
from karyo_profiles.journal_dns import JournalDNS,recover_profiles,file_receipt,unit_receipt
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
        "resolver_sha":hashlib.sha256(Path("/etc/resolv.conf").read_bytes()).hexdigest(),
        "forward":Path("/proc/sys/net/ipv4/ip_forward").read_text()}


from karyo_profiles.journal_session import JournalSession
from karyo_profiles.journal_session_record import validate_session
from karyo_profiles.user_manager import ManagerPending
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



EDITOR_FILE=Path("/home/kq-switch-work/karyo-journal-proof.txt")
EDITOR_MARKER="KaryoUnsavedJournalProof"


def start_editor(token):
    require(not EDITOR_FILE.exists(),"Synthetic editor target already exists")
    run("/usr/sbin/runuser","-u","kq-switch-work","--","/usr/bin/python3","-I","-c",
        "from pathlib import Path; Path('/home/kq-switch-work/karyo-journal-proof.txt').write_text('OriginalJournalFile\\n')")
    run("/usr/sbin/runuser","-u","kq-switch-work","--","/usr/bin/env",
        "XDG_RUNTIME_DIR=/run/user/1002","DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus",
        "/usr/bin/systemd-run","--user","--collect","--unit=karyo-qa-editor-"+token+".service",
        "--property=Type=exec","--","/usr/bin/karyo-editor",str(EDITOR_FILE))
    deadline=time.monotonic()+10
    while len(processes(1002,"karyo-editor"))!=1:
        require(time.monotonic()<deadline,"Manager-activated editor did not appear")
        time.sleep(.1)


def editor_barrier(phase):
    # The external driver types synthetic text through this VM's RFB console.
    # No credentials or real user documents are consumed by this fixed probe.
    print(json.dumps({"phase":phase}),flush=True)
    require(select.select([sys.stdin.buffer],[],[],30)[0],"Editor console acknowledgement timed out")
    require(sys.stdin.buffer.readline(17)==b"continue\n","Unexpected editor console acknowledgement")
    deadline=time.monotonic()+5
    while True:
        require(EDITOR_FILE.read_text()=="OriginalJournalFile\n","Editor saved the synthetic source unexpectedly")
        caches=list(Path("/home/kq-switch-work/.local/share/openkaryon/openkaryon-editor/buffers").glob("*/text.txt"))
        if len(caches)==1 and caches[0].read_text()==EDITOR_MARKER+"\n":break
        require(time.monotonic()<deadline,"Synthetic unsaved editor buffer is missing")
        time.sleep(.1)
    print(json.dumps({"unsaved_editor_preserved":phase,"cache_sha256":hashlib.sha256(caches[0].read_bytes()).hexdigest()}),flush=True)


def controller():
    request=json.loads(sys.stdin.buffer.readline(1025));require(set(request)=={"mode"} and request["mode"] in {"none","home"},"Unknown session QA mode")
    mode=request["mode"];store=ConfigStore();accounts=LocalAccounts();sessions=Sessions()
    child=accounts.account("kq-switch-work");require(child.uid==1002,"Unexpected QA child")
    source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"],"Prior session journal exists")
        with store.locked():
            network=JournalNetwork.start(journal,store,child,source)
            physical=discover_underlay(network.network.command)
            dns=JournalDNS.start(journal,network,discover_dns(physical,network.network.command))
            scope=JournalSession.start(journal,dns)
            scope.enter(accounts.account("karyoqa"))
        deadline=time.monotonic()+20
        while True:
            record=journal.record(scope.token);view=ui(record)
            with store.locked():ready=scope.graph.compositor_ready()
            if ready and view["active"] and ((mode=="none" and len(view["shell"])==1) or
                    (mode=="home" and view["locked"] and not view["shell"] and len(view["lock"])==1)):break
            require(time.monotonic()<deadline,"Initial owned desktop readiness timed out")
            time.sleep(.1)
        if mode=="none":
            start_editor(scope.token)
            view=ui(record)
        with store.locked():scope.verify_consumers()
        print(json.dumps({"ready":mode,"pid":os.getpid(),"record":record,"ui":view}),flush=True)
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


def host_reattach(record,expected_ui):
    require(unit_info()["ActiveState"]=="inactive","A host controller is already active")
    run("/usr/bin/systemctl","start","karyo-profile-host.socket");node=socket_node();unit=None
    try:
        result=subprocess.run(["/usr/bin/python3","-I",__file__,"--client"],input=json.dumps({"record":record}),
            text=True,capture_output=True,timeout=45)
        require(result.returncode==0,"Live Work host request failed: "+result.stderr)
        print(result.stdout.strip(),flush=True)
        unit=unit_info();require(unit["ActiveState"]=="active" and int(unit["MainPID"])>1,"Host registry failed to remain alive")
        require(ui(record)==expected_ui,"Host startup changed or unlocked the existing desktop")
        pidfds=[]
        for path in Path("/proc",unit["MainPID"],"fdinfo").iterdir():
            for line in path.read_text().splitlines():
                if line.startswith("Pid:"):pidfds.append(int(line.split()[1]))
        require(expected_ui["manager"] in pidfds and expected_ui["compositor"] in pidfds,"Registry did not retain both native process observers")
        try:RuntimeJournal()
        except JournalRetained:pass
        else:raise RuntimeError("The live host lost journal ownership")
        print(json.dumps({"host_reattached_live_gui":True,"host_pid":int(unit["MainPID"]),"ui":expected_ui}),flush=True)
    finally:
        if unit is not None:
            require(unit_info()==unit,"Captured host controller identity changed")
            run("/usr/bin/systemctl","stop","karyo-profile-host.service")
        require(unit_info()["ActiveState"]=="inactive","Uncaptured host controller retained")
        require(socket_node()==node,"Captured host socket was replaced")
        run("/usr/bin/systemctl","stop","karyo-profile-host.socket")


if sys.argv[1:]==["--controller"]:controller();raise SystemExit(0)
if sys.argv[1:]==["--client"]:client();raise SystemExit(0)

store=ConfigStore();sessions=Sessions();accounts=LocalAccounts()
source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
main_shell=processes(1001,"karyo-shell");require(len(main_shell)==1,"Original main shell is ambiguous")
original=snapshot()
original_accounts={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Prior resource records require review")
run("/usr/local/libexec/karyo-qa-two-sessions","prepare")
child=accounts.account("kq-switch-work");original_policy=store.settings(child.name)
try:
    for mode in ("none","home"):
        policy=DesktopSettings(password=mode)
        with store.locked():
            require(store.settings(child.name)==original_policy,"QA profile policy changed")
            store.save_settings(child.name,policy)
        with subprocess.Popen(["/usr/bin/python3","-I",__file__,"--controller"],stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,stderr=subprocess.PIPE) as process:
            process.stdin.write((json.dumps({"mode":mode})+chr(10)).encode());process.stdin.flush()
            try:
                require(select.select([process.stdout],[],[],35)[0],"Session controller readiness timeout")
                packet=process.stdout.readline(32769)
                if not packet:raise RuntimeError("Session controller failed: "+process.stderr.read(32768).decode())
                ready=json.loads(packet);require(ready["ready"]==mode and ready["pid"]==process.pid,"Unexpected captured session controller")
                record=ready["record"];expected_ui=ready["ui"]
                if mode=="none":editor_barrier("editor-ready")
                process.kill();require(process.wait(timeout=5)==-9,"Captured session controller did not terminate")
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
        with RuntimeJournal() as journal:
            with store.locked():
                observed=JournalSession.restore(journal,store,record)
                require(ui(record)==expected_ui,"Observer reattachment changed the UI")
                observed.release_handles()
        host_reattach(record,expected_ui)
        require(ui(record)==expected_ui,"Host observer release changed the desktop")
        if mode=="none":editor_barrier("editor-after")
        current=sessions.get(source.id)
        require(current is not None and (current.uid,current.name,current.vt)==(source.uid,source.name,source.vt),"Original return session changed")
        sessions.activate(source.id)
        with RuntimeJournal() as journal:
            with store.locked():observed=JournalSession.restore(journal,store,record)
            deadline=time.monotonic()+30
            while True:
                try:
                    with store.locked():observed.close()
                    break
                except ManagerPending:
                    require(time.monotonic()<deadline,"Native user manager did not finish naturally")
                    time.sleep(.1)
            with store.locked():observed.dns.close();observed.network.close()
            observed.release_handles()
            require(not journal.snapshot()["profiles"],"Session cleanup left a journal record")
        with store.locked():
            require(store.settings(child.name)==policy,"Known QA policy changed before restoration")
            store.save_settings(child.name,original_policy)
        require(snapshot()==original and processes(1001,"karyo-shell")==main_shell,"Original network or main GUI changed")
        print(json.dumps({"mode":mode,"result":"same live GUI reattached; known cleanup complete","ui":expected_ui}),flush=True)
finally:
    with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Retained session resources; do not remove the QA account")
    require(store.settings(child.name)==original_policy,"QA protection policy was not restored")
    run("/usr/local/libexec/karyo-qa-two-sessions","cleanup")

require({path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}==original_accounts,"Account/common PAM restoration failed")
require(snapshot()==original and sessions.get(source.id)==source and processes(1001,"karyo-shell")==main_shell,"Final source state changed")
require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-host","karyo-profile-session","karyo-profile-password-sync"),"Package verification failed")
require(not run("/usr/bin/dpkg","--audit"),"Package audit failed")
print("PASS installed living GUI journal reattachment, host registry and protected lock preservation",flush=True)
