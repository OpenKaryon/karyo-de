#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed disposable-host journaled forwarding, private DNS and GUI lifetime acceptance."""
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
from karyo_profiles.journal_network import JournalNetwork
from karyo_profiles.journal_forwarding import JournalForwarding
from karyo_profiles.networks import NetworkSpec,NetworkCommand,NetworkError
from karyo_profiles.forwarding import _canonical,_table
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
        "forward":Path("/proc/sys/net/ipv4/ip_forward").read_text(),
        "ipv4_conf":{str(p):p.read_text() for p in Path("/proc/sys/net/ipv4/conf").glob("*/*")}}


from karyo_profiles.journal_session import JournalSession
from karyo_profiles.journal_session_record import validate_session
from karyo_profiles.model import DesktopSettings


HOME_QUERY="import json,secrets,socket,struct,sys\nident=secrets.randbelow(65536)\npacket=struct.pack('!6H',ident,256,1,0,0,0)+b'\\x07example\\x03org\\0'+struct.pack('!HH',1,1)\nfor attempt in range(1,7):\n with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as connection:\n  connection.settimeout(1)\n  connection.connect((sys.argv[1],53))\n  connection.send(packet)\n  try:reply=connection.recv(4096)\n  except TimeoutError:\n   if attempt==6:raise\n   continue\n  break\nheader=struct.unpack('!6H',reply[:12])\nassert header[0]==ident and header[1]&32768 and not header[1]&15 and header[3]>0\nprint(json.dumps({'rcode':header[1]&15,'answers':header[3],'attempts':attempt}))\n"

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
    request=json.loads(sys.stdin.buffer.readline(1025));require(set(request)=={"mode"} and request["mode"] in {"none","home"},"Unknown session QA mode")
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
    import signal
    signal.alarm(12)
    answers=socket.getaddrinfo("example.org",443,socket.AF_INET,socket.SOCK_STREAM)
    require(bool(answers),"Private mounted resolver returned no IPv4 answers")
    with socket.create_connection(("1.1.1.1",443),timeout=4) as connection:source=connection.getsockname()[0]
    require(source==NetworkSpec(account.uid,record["token"]).guest,"GUI-context TCP did not use its private source")
    signal.alarm(0)
    print(json.dumps({"family_listing_from_live_work":True,"private_dns_answers":len(answers),"tcp_source":source}),flush=True)


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


def supervise(record,source,mode):
    require(unit_info()["ActiveState"]=="inactive","A host controller is already active")
    run("/usr/bin/systemctl","start","karyo-profile-host.socket");node=socket_node();unit=None
    try:
        result=subprocess.run(["/usr/bin/python3","-I",__file__,"--client"],input=json.dumps({"record":record}),text=True,capture_output=True,timeout=35)
        require(result.returncode==0,"Live host request failed: "+result.stderr)
        packets=json.loads(result.stdout)
        require(packets["private_dns_answers"]>0,"No private GUI-context packet evidence")
        unit=unit_info();require(unit["ActiveState"]=="active" and int(unit["MainPID"])>1,"Native host registry is not active")
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
        print(json.dumps({"mode":mode,"host_pid":int(unit["MainPID"]),"event":events[0],"home":source.id,"vt":source.vt,"cleanup":"complete","private_packets":packets}),flush=True)
    finally:
        if unit is not None:
            require(unit_info()==unit,"Captured supervision host changed")
            run("/usr/bin/systemctl","stop","karyo-profile-host.service")
        require(unit_info()["ActiveState"]=="inactive","Uncaptured host controller retained")
        require(socket_node()==node,"Captured host socket changed")
        run("/usr/bin/systemctl","stop","karyo-profile-host.socket")


if sys.argv[1:]==["--controller"]:controller();raise SystemExit(0)
if sys.argv[1:]==["--client"]:client();raise SystemExit(0)

store=ConfigStore();sessions=Sessions();accounts=LocalAccounts()
source=next(row for row in sessions.all() if row.uid==1001 and row.name=="karyoqa" and row.active)
main_shell=processes(1001,"karyo-shell");require(len(main_shell)==1,"Original main shell is ambiguous")
original=snapshot()
original_accounts={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
    [Path("/etc/passwd"),Path("/etc/shadow"),Path("/etc/group"),Path("/etc/gshadow"),*Path("/etc/pam.d").glob("common-*")]}
with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Prior resources need review")
run("/usr/local/libexec/karyo-qa-two-sessions","prepare")
child=accounts.account("kq-switch-work");original_policy=store.settings(child.name)
command=NetworkCommand();selected=discover_underlay(command);vpn=SyntheticVPN(command,selected)
try:
    Path("/proc/sys/net/ipv4/ip_forward").write_text("1\n")
    vpn.create()
    try:command.run("/usr/sbin/ip","-4","route","get","1.1.1.1")
    except NetworkError:pass
    else:raise RuntimeError("Synthetic Home external route was not blocked")
    try:command.run("/usr/bin/python3","-I","-c",HOME_QUERY,selected.gateway)
    except NetworkError:pass
    else:raise RuntimeError("Synthetic Home direct DNS was not blocked")
    print(json.dumps({"home_policy_blocked":True,"home_dns_blocked":True}),flush=True)
    baseline=snapshot()
    for mode in ("logout","crash","background","locked-crash"):
        policy=DesktopSettings(password="home" if mode=="locked-crash" else "none")
        with store.locked():
            require(store.settings(child.name)==original_policy,"QA policy changed")
            store.save_settings(child.name,policy)
        with subprocess.Popen(["/usr/bin/python3","-I",__file__,"--controller"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as process:
            process.stdin.write((json.dumps({"mode":policy.password})+chr(10)).encode());process.stdin.flush()
            try:
                require(select.select([process.stdout],[],[],35)[0],"Observed desktop controller readiness timeout")
                packet=process.stdout.readline(32769)
                if not packet:raise RuntimeError("Observed controller failed: "+process.stderr.read(32768).decode())
                ready=json.loads(packet);require(ready["pid"]==process.pid and ready["ready"]==policy.password,"Captured controller changed")
                record=ready["record"]
                process.kill();require(process.wait(timeout=5)==-9,"Captured controller did not terminate")
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
        supervise(record,source,mode)
        with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Supervisor left ownership records")
        with store.locked():
            require(store.settings(child.name)==policy,"QA policy changed before restoration")
            store.save_settings(child.name,original_policy)
        require(snapshot()==baseline and processes(1001,"karyo-shell")==main_shell,"Original network/main GUI changed")
finally:
    try:vpn.close()
    finally:
        Path("/proc/sys/net/ipv4/ip_forward").write_text(original["forward"])
        for name,value in original["ipv4_conf"].items():
            path=Path(name)
            if path.read_text()!=value:path.write_text(value)
    with RuntimeJournal() as journal:require(not journal.snapshot()["profiles"],"Retained resources; do not remove QA account")
    require(store.settings(child.name)==original_policy,"QA policy not restored")
    run("/usr/local/libexec/karyo-qa-two-sessions","cleanup")

require({path:hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in original_accounts}==original_accounts,"Account/common PAM restoration failed")
require(snapshot()==original and sessions.get(source.id)==source and processes(1001,"karyo-shell")==main_shell,"Final original source changed")
require(not run("/usr/bin/dpkg","-V","karyo-profile-accounts","karyo-profile-host","karyo-profile-session","karyo-profile-password-sync"),"Package verification failed")
require(not run("/usr/bin/dpkg","--audit"),"Package audit failed")
print("PASS installed forwarding/private-DNS GUI reattachment, packets, logout/crash and Home return",flush=True)
