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


def controller():
    packet=sys.stdin.buffer.readline(1025)
    require(len(packet)<=1024 and packet.endswith(bytes([10])),"Bounded DNS controller input required")
    request=json.loads(packet)
    require(isinstance(request,dict) and set(request)=={"mode"} and request["mode"] in
        {"clean","active","resolver-stopped","files-partial","files-gone","host-restart"},"Unknown DNS controller mode")
    mode=request["mode"];store=ConfigStore();accounts=LocalAccounts();sessions=Sessions()
    child=accounts.account("kq-switch-work");require(child.uid==1002,"Unexpected QA child UID")
    source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"],"Existing DNS journal resources require review")
        with store.locked():
            network=JournalNetwork.start(journal,store,child,source)
            physical=discover_underlay(network.network.command)
            scope=JournalDNS.start(journal,network,discover_dns(physical,network.network.command))
            if mode=="resolver-stopped":
                scope.update(pending="resolver-stop");scope.services.resolver.close()
            elif mode in {"files-partial","files-gone"}:
                scope.services.close(before=scope.before_stop,after=scope.after_stop)
                scope.update(pending="files-remove");scope.files.verify()
                if mode=="files-partial":os.unlink("bus.conf",dir_fd=scope.files.fd)
                else:scope.files.close()
        print(json.dumps({"ready":mode,"pid":os.getpid(),"record":journal.record(network.token)}),flush=True)
        acknowledgement=sys.stdin.buffer.read(1)
        if mode=="clean" and acknowledgement==b"x":
            with store.locked():scope.close();network.close()
            print(json.dumps({"clean":True}),flush=True)
            return
        raise RuntimeError("The crash branch requires termination of this captured controller")

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


def reattach_and_close(journal,store,record):
    with store.locked():
        network=restore_network(store,record,require_empty=False)
        require(network is not None,"DNS network vanished")
        scope=None
        try:
            wrapped=JournalNetwork(journal,network);scope=JournalDNS.restore(journal,wrapped)
            scope.services.verify()
            require(file_receipt(scope.files)==record["dns"]["files"],"Configuration identities changed during reattachment")
            for role in ("bus","resolver"):
                require(unit_receipt(scope.files,role,getattr(scope.services,role))==record["dns"][role],"Live DNS service was replaced or restarted")
            actual=json.loads(run("/usr/bin/busctl","--address="+scope.files.plan.bus_address,"--json=short","--timeout=5s",
                "call","org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetConnectionUnixProcessID","s","org.freedesktop.resolve1"))
            require(actual=={"type":"u","data":[record["dns"]["resolver"]["pid"]]},"Private bus name owner changed")
            print(json.dumps({"reattached_without_restart":True,"bus_pid":record["dns"]["bus"]["pid"],"resolver_pid":record["dns"]["resolver"]["pid"]}),flush=True)
            scope.close();wrapped.close()
        finally:
            if scope is not None:scope.release_handles()
            if network.fd is not None:os.close(network.fd);network.fd=None


store=ConfigStore();sessions=Sessions()
source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
original=snapshot()
original_accounts={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
for unit in ("karyo-profile-host.service","karyo-profile-host.socket"):
    require(run("/usr/bin/systemctl","show",unit,"-p","ActiveState","--value")=="inactive","Host transport still active")
with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Previous resources must not be adopted")
run("/usr/local/libexec/karyo-qa-two-sessions","prepare")
try:
    for mode in ("clean","active","resolver-stopped","files-partial","files-gone","host-restart"):
        with subprocess.Popen(["/usr/bin/python3","-I",__file__,"--controller"],stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,stderr=subprocess.PIPE) as process:
            process.stdin.write((json.dumps({"mode":mode})+chr(10)).encode());process.stdin.flush()
            try:
                require(select.select([process.stdout],[],[],25)[0],"DNS controller readiness timeout")
                packet=process.stdout.readline(16385)
                if not packet:raise RuntimeError("DNS controller failed before receipt: "+process.stderr.read(16384).decode())
                require(len(packet)<=16384,"DNS controller receipt exceeds limit")
                ready=json.loads(packet);require(ready["ready"]==mode and ready["pid"]==process.pid,"Unexpected DNS controller")
                record=ready["record"]
                try:RuntimeJournal()
                except JournalRetained:pass
                else:raise RuntimeError("Concurrent DNS controller acquired the lifetime lock")
                if mode=="clean":
                    process.stdin.write(b"x");process.stdin.flush()
                    require(process.wait(timeout=15)==0,"Normal DNS cleanup failed: "+process.stderr.read(16384).decode())
                else:process.kill();require(process.wait(timeout=5)==-9,"Captured DNS controller did not terminate")
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
        if mode=="host-restart":host_recovery(record)
        with RuntimeJournal() as journal:
            if mode not in {"clean","host-restart"}:
                require(journal.snapshot()["profiles"]==[record],"DNS crash lost its captured journal")
                if mode=="active":reattach_and_close(journal,store,record)
                else:recover_profiles(journal,store)
            require(not journal.snapshot()["profiles"],"DNS recovery left an ownership record")
        require(snapshot()==original and sessions.get(source.id)==source,"Network or main GUI changed")
        token=record["token"]
        for path in ("/run/karyo/resolver-config/"+token,"/run/karyo-resolve-"+token,
                "/run/karyo-resolve-"+token+"-netif","/run/karyo-resolve-bus-"+token):
            require(not os.path.lexists(path),"Captured DNS runtime/configuration remains")
        print(json.dumps({"mode":mode,"result":"clean","token":token}),flush=True)
finally:
    with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Retained DNS resources; do not remove the QA account")
    run("/usr/local/libexec/karyo-qa-two-sessions","cleanup")

require({path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}==original_accounts,"Accounts/common PAM changed")
require(snapshot()==original and sessions.get(source.id)==source,"Network or original login changed")
require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-host","karyo-profile-session","karyo-profile-password-sync"),"Package verification failed")
require(not run("/usr/bin/dpkg","--audit"),"Package audit failed")
print("PASS installed DNS journal reattachment, interrupted cleanup and host startup recovery",flush=True)
