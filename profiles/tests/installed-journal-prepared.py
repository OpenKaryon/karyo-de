#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host offline preparation crash acceptance."""
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
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.journal_dns import JournalDNS
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

from karyo_profiles.journal_session import SessionRegistry,manager_receipt
from karyo_profiles.user_manager import UserManagerScope


def controller():
    request=json.loads(sys.stdin.buffer.readline(1025))
    require(set(request)=={"mode"} and request["mode"] in {"prepared","login-start","host"},"Unknown preparation mode")
    store=ConfigStore();accounts=LocalAccounts();sessions=Sessions()
    child=accounts.account("kq-switch-work");require(child.uid==1002,"Unexpected QA child")
    source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"],"Prior ownership journal exists")
        with store.locked():
            network=JournalNetwork.start(journal,store,child,source)
            physical=discover_underlay(network.network.command)
            dns=JournalDNS.start(journal,network,discover_dns(physical,network.network.command))
            before=journal.record(dns.token)
            journal.change(before,dict(before,session={"pending":"manager-prepare","manager":None,"graphical":None}))
            manager=UserManagerScope(dns.files).prepare()
            before=journal.record(dns.token)
            journal.change(before,dict(before,session={"pending":None if request["mode"]=="prepared" else "login-start",
                           "manager":manager_receipt(manager),"graphical":None}))
        print(json.dumps({"pid":os.getpid(),"mode":request["mode"],"record":journal.record(dns.token)}),flush=True)
        sys.stdin.buffer.read(1)
        raise RuntimeError("The fixed driver must terminate this controller")

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


if sys.argv[1:]:controller();raise SystemExit(0)

store=ConfigStore();sessions=Sessions();accounts=LocalAccounts()
source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
original=snapshot()
original_accounts={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Prior journal requires review")
run("/usr/local/libexec/karyo-qa-two-sessions","prepare")
try:
    for mode in ("prepared","login-start","host"):
        with subprocess.Popen(["/usr/bin/python3","-I",__file__,"--controller"],stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,stderr=subprocess.PIPE) as process:
            process.stdin.write((json.dumps({"mode":mode})+chr(10)).encode());process.stdin.flush()
            try:
                require(select.select([process.stdout],[],[],20)[0],"Prepared controller readiness timeout")
                packet=process.stdout.readline(32769)
                if not packet:raise RuntimeError("Prepared controller failed: "+process.stderr.read(32768).decode())
                ready=json.loads(packet)
                require(ready["pid"]==process.pid and ready["mode"]==mode,"Captured preparation child changed")
                record=ready["record"]
                try:RuntimeJournal()
                except JournalRetained:pass
                else:raise RuntimeError("Live preparation controller lost ownership")
                process.kill();require(process.wait(timeout=5)==-9,"Captured preparation controller did not stop")
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
        if mode=="host":host_recovery(record)
        else:
            with RuntimeJournal() as journal:
                require(journal.snapshot()["profiles"]==[record],"Prepared journal changed before recovery")
                registry=SessionRegistry(journal,store).restore()
                require(not registry.profiles and not journal.snapshot()["profiles"],"Preparation recovery retained resources")
                registry.release_handles()
        require(not Path("/run/systemd/system/user@1002.service.d").exists(),"Captured prepared policy remains")
        require(not Path("/run/user/1002").exists() and not sessions.owners(1002),"Recovery created a user manager/login")
        require(snapshot()==original and sessions.get(source.id)==source,"Preparation recovery changed the original network/login")
        print(json.dumps({"mode":mode,"result":"captured preparation recovered without GUI startup"}),flush=True)
finally:
    with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Retained ownership; do not remove QA account")
    run("/usr/local/libexec/karyo-qa-two-sessions","cleanup")

require({path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}==original_accounts,"Account/common PAM restoration failed")
require(snapshot()==original and sessions.get(source.id)==source,"Final original source changed")
require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-host","karyo-profile-session","karyo-profile-password-sync"),"Package verification failed")
require(not run("/usr/bin/dpkg","--audit"),"Package audit failed")
print("PASS installed preparation SIGKILL and actual host startup recovery",flush=True)
