#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host forwarding journal packet and SIGKILL acceptance."""
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
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.dns import discover_dns
from karyo_profiles.underlay import discover_underlay
from karyo_profiles.logind import Sessions
from karyo_profiles.networks import NetworkCommand,NetworkError
from karyo_profiles.forwarding import _canonical,_table
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
        "forward":Path("/proc/sys/net/ipv4/ip_forward").read_text(),
        "ipv4_conf":{str(p):p.read_text() for p in Path("/proc/sys/net/ipv4/conf").glob("*/*")}}

from karyo_profiles.journal_forwarding import JournalForwarding

QUERY="import json,secrets,socket,struct,sys\nident=secrets.randbelow(65536)\npacket=struct.pack('!6H',ident,256,1,0,0,0)+b'\\x07example\\x03org\\0'+struct.pack('!HH',1,1)\nfor attempt in range(1,7):\n with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as connection:\n  connection.settimeout(1)\n  connection.connect((sys.argv[1],53))\n  connection.send(packet)\n  try:reply=connection.recv(4096)\n  except TimeoutError:\n   if attempt==6:raise\n   continue\n  break\nheader=struct.unpack('!6H',reply[:12])\nassert header[0]==ident and header[1]&32768 and not header[1]&15 and header[3]>0\nprint(json.dumps({'rcode':header[1]&15,'answers':header[3],'attempts':attempt}))\n"
TCP="import socket; connection=socket.create_connection(('1.1.1.1',443),timeout=4); print(connection.getsockname()[0]); connection.close()"


class SyntheticVPN:
    def __init__(self, command, physical):
        self.command, self.physical = command, physical
        self.table, self.priority = 59000, 100
        self.records, self.expected = [], []
        self.rule = None
        self.uncertain = False

    def ip(self, *args):
        return self.command.run("/usr/sbin/ip", "-4", *args)

    def routes(self):
        return [row for row in json.loads(self.ip("-j", "-N", "route", "show", "table", "all"))
                if _table(row.get("table", 254)) == self.table]

    def rules(self):
        return [row for row in json.loads(self.ip("-j", "-N", "rule", "show"))
                if row.get("priority") == self.priority or ("table" in row and _table(row["table"]) == self.table)]

    def create(self):
        require(not self.routes() and not self.rules(), "Synthetic VPN fixture slots are occupied")
        for args in (("unreachable", "default", "metric", "32767"),
                     (self.physical.subnet, "dev", self.physical.interface, "scope", "link"),
                     ("unreachable", self.physical.gateway + "/32", "metric", "1")):
            self.uncertain = True
            self.ip("route", "add", *args, "table", str(self.table), "proto", "static")
            current = self.routes()
            require(len(current) == len(self.expected) + 1 and all(row in current for row in self.expected),
                    "Synthetic VPN route identity changed")
            self.records.append(args)
            self.expected = current
            self.uncertain = False
        self.uncertain = True
        self.ip("rule", "add", "priority", str(self.priority), "table", str(self.table))
        rows = self.rules()
        require(rows == [{"priority": 100, "src": "all", "table": "59000"}], "Synthetic VPN rule changed")
        self.rule = rows[0]
        self.uncertain = False

    def close(self):
        require(not self.uncertain and _canonical(self.routes()) == _canonical(self.expected),
                "Retained synthetic VPN fixture routes")
        require(self.rules() == ([] if self.rule is None else [self.rule]), "Retained synthetic VPN rule")
        if self.rule is not None:
            self.ip("rule", "delete", "priority", str(self.priority), "table", str(self.table))
            self.rule = None
        for args in reversed(self.records):
            self.ip("route", "delete", *args, "table", str(self.table), "proto", "static")
        require(not self.routes() and not self.rules(), "Synthetic VPN fixture cleanup incomplete")


def packet(network,physical):
    network.verify()
    reply=json.loads(network.command.run("/usr/bin/nsenter","--net="+network.path,"--","/usr/bin/python3","-I","-c",QUERY,physical.gateway))
    source=network.command.run("/usr/bin/nsenter","--net="+network.path,"--","/usr/bin/python3","-I","-c",TCP).strip()
    require(reply["rcode"]==0 and reply["answers"]>0 and source==network.spec.guest,"Private DNS/TCP packet verification failed")
    return {"dns":reply,"external_tcp":True,"source":source}


def controller():
    request=json.loads(sys.stdin.buffer.readline(1025));mode=request.get("mode")
    require(set(request)=={"mode"} and mode in {"clean","active","host",*("step-"+str(n) for n in range(1,8))},"Unknown forwarding mode")
    store=ConfigStore();accounts=LocalAccounts();sessions=Sessions()
    child=accounts.account("kq-switch-work");require(child.uid==1002,"Unexpected QA child")
    source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
    with RuntimeJournal() as journal:
        require(not journal.snapshot()["profiles"],"Prior ownership journal exists")
        with store.locked():
            network=JournalNetwork.start(journal,store,child,source)
            scope=JournalForwarding.start(journal,network)
            def ready():
                print(json.dumps({"pid":os.getpid(),"mode":mode,"record":journal.record(network.token)}),flush=True)
                sys.stdin.buffer.read(1)
            if mode.startswith("step-"):
                original=scope.capture;count=0
                def interrupted_ack():
                    nonlocal count
                    count+=1
                    if count==int(mode[5:]):ready();raise RuntimeError("Fixed driver must terminate controller")
                    original()
                scope.capture=interrupted_ack
                scope.close()
                raise RuntimeError("The native deletion boundary was not reached")
            ready()
            if mode=="clean":scope.close();network.close();return
            raise RuntimeError("Fixed driver must terminate controller")

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
require(original["forward"].strip()=="0","This fixed QA fixture expects forwarding initially off")
original_accounts={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Prior journal requires review")
run("/usr/local/libexec/karyo-qa-two-sessions","prepare")
command=NetworkCommand();selected=discover_underlay(command);vpn=SyntheticVPN(command,selected)
try:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("1\n")
    vpn.create()
    try:command.run("/usr/sbin/ip","-4","route","get","1.1.1.1")
    except NetworkError:pass
    else:raise RuntimeError("Synthetic Home policy did not block its external route")
    try:command.run("/usr/bin/python3","-I","-c",QUERY,selected.gateway)
    except NetworkError:pass
    else:raise RuntimeError("Synthetic Home policy did not block its direct DNS")
    print(json.dumps({"home_policy_blocked":True,"home_dns_blocked":True}),flush=True)
    baseline=snapshot()
    for mode in ("clean","active",*("step-"+str(n) for n in range(1,8)),"host"):
        with subprocess.Popen(["/usr/bin/python3","-I",__file__,"--controller"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as process:
            process.stdin.write((json.dumps({"mode":mode})+chr(10)).encode());process.stdin.flush()
            try:
                require(select.select([process.stdout],[],[],20)[0],"Forwarding controller readiness timed out")
                line=process.stdout.readline(65537)
                if not line:raise RuntimeError("Forwarding controller failed: "+process.stderr.read(32768).decode())
                ready=json.loads(line);require(ready["pid"]==process.pid and ready["mode"]==mode,"Captured forwarding controller changed")
                record=ready["record"]
                try:RuntimeJournal()
                except JournalRetained:pass
                else:raise RuntimeError("Live controller lost journal ownership")
                if mode=="clean":
                    process.stdin.write(b"x");process.stdin.flush()
                    output,error=process.communicate(timeout=20)
                    require(process.returncode==0,"Clean forwarding close failed: "+error.decode())
                else:process.kill();require(process.wait(timeout=5)==-9,"Captured forwarding controller did not terminate")
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
        evidence={}
        if mode=="host":host_recovery(record)
        elif mode!="clean":
            with RuntimeJournal() as journal:
                require(journal.snapshot()["profiles"]==[record],"Forwarding journal changed before recovery")
                with store.locked():
                    network=restore_network(store,record)
                    require(network is not None,"Captured forwarding network disappeared")
                    wrapped=JournalNetwork(journal,network)
                    try:
                        scope=JournalForwarding.restore(journal,wrapped)
                        if mode=="active":
                            require(journal.record(wrapped.token)==record,"Live forwarding reattachment changed receipt")
                            scope.verify();evidence=packet(network,scope.gateway.physical)
                        scope.close();wrapped.close()
                    finally:
                        if network.fd is not None:os.close(network.fd);network.fd=None
        with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Forwarding recovery retained ownership")
        require(snapshot()==baseline and sessions.get(source.id)==source,"Forwarding recovery changed original host state")
        print(json.dumps({"mode":mode,"result":"native forwarding journal recovered","packets":evidence}),flush=True)
finally:
    try:vpn.close()
    finally:
        Path("/proc/sys/net/ipv4/ip_forward").write_text(original["forward"])
        for name,value in original["ipv4_conf"].items():
            path=Path(name)
            if path.read_text()!=value:path.write_text(value)
    with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Retained resources; do not remove QA account")
    run("/usr/local/libexec/karyo-qa-two-sessions","cleanup")

require({path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}==original_accounts,"Account/common PAM restoration failed")
require(snapshot()==original and sessions.get(source.id)==source,"Final original host state changed")
require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-host","karyo-profile-session","karyo-profile-password-sync"),"Package verification failed")
require(not run("/usr/bin/dpkg","--audit"),"Package audit failed")
print("PASS installed forwarding journal clean/reattach/7-delete-SIGKILL/host recovery and isolated DNS/TCP with blocked Home",flush=True)
