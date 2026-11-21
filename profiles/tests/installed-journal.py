#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host crash acceptance for installed network ownership journals."""
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
        {"karyo-de-acceptance","karyo-de-ubuntu-acceptance"} or sys.argv[1:] not in ([],["--controller"])):
    raise SystemExit("Use only the fixed disposable hosts as root")
sys.dont_write_bytecode=True
sys.path.insert(0,"/usr/libexec/karyo/profiles")
from karyo_profiles.dispatcher import ADDRESS as SOCKET_ADDRESS,socket_node
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.journal import RuntimeJournal,JournalRetained
from karyo_profiles.journal_network import JournalNetwork,recover_networks
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


def controller():
    packet=sys.stdin.buffer.readline(1025)
    require(len(packet)<=1024 and packet.endswith(bytes([10])),"Bounded controller fixture input required")
    request=json.loads(packet)
    require(isinstance(request,dict) and set(request)=={"mode"} and request["mode"] in
        {"active","pair-removed","namespace-removed","host-restart","uncertain"},"Unknown controller fixture mode")
    mode=request["mode"]
    store=ConfigStore();accounts=LocalAccounts();sessions=Sessions()
    child=accounts.account("kq-switch-work");require(child.uid==1002,"Unexpected QA child UID")
    source=next(row for row in sessions.all() if row.name=="karyoqa" and row.uid==1001 and row.active)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"],"Existing journal must not be adopted")
        with store.locked():
            if mode=="uncertain":
                token=journal.begin(child,source)
            else:
                lease=JournalNetwork.start(journal,store,child,source,accounts=accounts,sessions=sessions)
                token=lease.token
                if mode in {"pair-removed","namespace-removed"}:
                    record=journal.record(token);journal.change(record,dict(record,pending="remove"))
                    if mode=="pair-removed":
                        lease.network._ip("link","delete","dev",lease.network.spec.interface)
                    else:lease.network.close()
        print(json.dumps({"ready":mode,"record":journal.record(token),"pid":os.getpid()}),flush=True)
        # The driver deliberately SIGKILLs only this captured child. Do not
        # close resources here: descriptors disappear exactly as in a crash.
        sys.stdin.buffer.read(1)
        raise RuntimeError("The crash fixture requires its driver")

def unit_info():
    return {line.split("=",1)[0]:line.split("=",1)[1] for line in run("/usr/bin/systemctl","show",
        "karyo-profile-host.service","-p","InvocationID","-p","MainPID","-p","ActiveState").splitlines()}


def host_recovery(expected):
    with RuntimeJournal() as journal:
        require(journal.snapshot()["profiles"]==[expected],"Unexpected journal before host restart")
    run("/usr/bin/systemctl","start","karyo-profile-host.socket")
    node=socket_node();unit=None
    try:
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as trigger:
            trigger.settimeout(2);trigger.connect(SOCKET_ADDRESS)
            deadline=time.monotonic()+10
            while True:
                current=unit_info()
                if current["ActiveState"]=="active" and int(current["MainPID"])>1 and len(current["InvocationID"])==32:
                    unit=current;break
                require(time.monotonic()<deadline,"Host service did not activate for recovery")
                time.sleep(.05)
        state=Path("/run/karyo/profile-host/state.json")
        while True:
            require(unit_info()==unit,"Host recovery service identity changed")
            info=state.stat();require(info.st_uid==0 and info.st_nlink==1 and info.st_mode&0o777==0o600 and info.st_size<=1024*1024,"Unsafe recovered journal")
            if not json.loads(state.read_bytes())["profiles"]:break
            require(time.monotonic()<deadline,"Host startup did not reconcile the captured network")
            time.sleep(.05)
        try:RuntimeJournal()
        except JournalRetained:pass
        else:raise RuntimeError("Running host did not retain journal ownership")
        print(json.dumps({"host_startup_recovered":True,"host_pid":int(unit["MainPID"])}),flush=True)
    finally:
        if unit is not None:
            current=unit_info()
            require(current["InvocationID"]==unit["InvocationID"] and current["MainPID"] in {unit["MainPID"],"0"},"Changed host unit retained")
            run("/usr/bin/systemctl","stop","karyo-profile-host.service")
        require(unit_info()["ActiveState"]=="inactive","Uncaptured host service retained")
        require(socket_node()==node,"Changed host socket retained")
        run("/usr/bin/systemctl","stop","karyo-profile-host.socket")


if sys.argv[1:]:
    controller();raise SystemExit(0)

store=ConfigStore();sessions=Sessions();accounts=LocalAccounts()
source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
original=snapshot()
original_accounts={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
for unit in ("karyo-profile-host.service","karyo-profile-host.socket"):
    require(run("/usr/bin/systemctl","show",unit,"-p","ActiveState","--value")=="inactive","Host transport still active")
with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Prior journal resources require review")
run("/usr/local/libexec/karyo-qa-two-sessions","prepare")
prepared=True
try:
    for mode in ("active","pair-removed","namespace-removed","host-restart","uncertain"):
        with subprocess.Popen(["/usr/bin/python3","-I",__file__,"--controller"],
                stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as process:
            process.stdin.write((json.dumps({"mode":mode})+chr(10)).encode());process.stdin.flush()
            try:
                require(select.select([process.stdout],[],[],20)[0],"Controller readiness timeout")
                packet=process.stdout.readline(16385)
                if not packet:
                    raise RuntimeError("Controller failed before receipt: "+process.stderr.read(16384).decode())
                require(len(packet)<=16384,"Controller receipt exceeds limit")
                ready=json.loads(packet)
                require(ready["ready"]==mode and ready["pid"]==process.pid,"Unexpected controller receipt")
                record=ready["record"]
                try:RuntimeJournal()
                except JournalRetained:pass
                else:raise RuntimeError("A live second controller acquired the journal")
                process.kill();require(process.wait(timeout=5)==-9,"Captured controller did not stop")
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
        if mode=="host-restart":host_recovery(record)
        with RuntimeJournal() as journal:
            if mode=="host-restart":
                require(not journal.snapshot()["profiles"],"Host startup left profile resources")
                result="host-service-recovered"
            else:require(journal.snapshot()["profiles"]==[record],"Crash lost or changed the resource receipt")
            if mode=="uncertain":
                before=Path(journal.path,"state.json").read_bytes()
                try:recover_networks(journal,store)
                except JournalRetained:pass
                else:raise RuntimeError("An unacknowledged creation was adopted")
                require(Path(journal.path,"state.json").read_bytes()==before,"Uncertain creation record was overwritten")
                require(snapshot()==original,"The fixed no-mutation fixture unexpectedly created resources")
                # This known fixture branch only called journal.begin, never
                # a native mutation. Clear its exact synthetic record, without
                # adding any generic acknowledge/forget production endpoint.
                journal.change(record,None)
                result="retained-without-mutation"
            elif mode!="host-restart":
                recover_networks(journal,store)
                require(not journal.snapshot()["profiles"],"Recovery did not close the captured receipt")
                result="recovered"
            require((journal.path.stat().st_mode&0o777)==0o700,"Journal directory is not private")
            require((Path(journal.path,"state.json").stat().st_mode&0o777)==0o600,"Journal state is not private")
        require(snapshot()==original,"Network state changed after crash/recovery")
        require(sessions.get(source.id)==source,"Original graphical login changed")
        print(json.dumps({"mode":mode,"result":result,"token":record["token"],"source_vt":source.vt}),flush=True)
finally:
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"],"Pending resources retained; do not remove QA account")
    if prepared:run("/usr/local/libexec/karyo-qa-two-sessions","cleanup")

require({path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}==original_accounts,"Accounts/common PAM were not restored")
require(snapshot()==original and sessions.get(source.id)==source,"Network or source login changed")
require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-host","karyo-profile-session","karyo-profile-password-sync"),"Package verification failed")
require(not run("/usr/bin/dpkg","--audit"),"Package audit failed")
print("PASS installed journal SIGKILL recovery, interrupted cleanup and uncertain creation retention",flush=True)
