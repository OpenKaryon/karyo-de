#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in private resolved/real libc client probe on two disposable QA VMs."""
import hashlib
import json
import os
from pathlib import Path
import re
import select
import signal
import socket
import stat
import subprocess
import sys
import time
import uuid

sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.dns import ResolverFiles, discover_dns, seed_private_dns
from karyo_profiles.forwarding import ProfileForwarding
from karyo_profiles.networks import NetworkCommand, NetworkError, create_network
from karyo_profiles.storage import ConfigStore
from karyo_profiles.underlay import discover_underlay


def require(value, message):
    if not value:
        raise RuntimeError(message)


class Unit:
    def __init__(self, command, name, executable, uid, network_identity=None):
        self.command, self.name, self.executable, self.uid = command, name, executable, uid
        self.network_identity = network_identity
        self.invocation = None
        self.pid = None
        self.pidfd = None
        self.cgroup = None
        self.closed = False

    def info(self):
        result = subprocess.run(["/usr/bin/systemctl", "show", self.name, "--no-pager",
                                 "--property=LoadState", "--property=MainPID", "--property=InvocationID",
                                 "--property=ControlGroup", "--property=Transient"],
                                capture_output=True, text=True, timeout=5)
        rows = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
        require(rows.get("LoadState") in {"loaded", "not-found"}, "Unexpected native unit state: " + self.name)
        return rows

    def start(self, properties, *arguments):
        require(self.info()["LoadState"] == "not-found", "Refuse an existing native unit: " + self.name)
        options = ["/usr/bin/systemd-run", "--quiet", "--collect", "--unit=" + self.name, "--property=Type=exec"]
        for value in properties:
            options.append("--property=" + value)
        self.command.run(*options, "--", self.executable, *arguments)
        rows = self.info()
        require(rows.get("Transient") == "yes" and re.fullmatch(r"[a-f0-9]{32}", rows.get("InvocationID", "")),
                "The fresh transient unit identity was not captured: " + self.name)
        self.invocation = rows["InvocationID"]
        self.cgroup = rows["ControlGroup"]
        self.pid = int(rows["MainPID"])
        require(self.pid > 1, "Native service exited before ownership capture: " + self.name)
        self.pidfd = os.pidfd_open(self.pid)
        self.verify()

    def verify(self):
        rows = self.info()
        require(rows.get("InvocationID") == self.invocation and rows.get("ControlGroup") == self.cgroup
                and rows.get("MainPID") == str(self.pid), "Native unit ownership changed: " + self.name)
        require(not select.select([self.pidfd], [], [], 0)[0], "Owned native process exited: " + self.name)
        require(os.path.realpath("/proc/" + str(self.pid) + "/exe") == os.path.realpath(self.executable),
                "Unexpected native service executable")
        status = Path("/proc/" + str(self.pid) + "/status").read_text()
        uid = next(line for line in status.splitlines() if line.startswith("Uid:")).split()[1:]
        require(uid == [str(self.uid)] * 4, "Unexpected native service credentials")
        require("0::" + self.cgroup in Path("/proc/" + str(self.pid) + "/cgroup").read_text().splitlines(),
                "Unexpected native service cgroup")
        if self.network_identity:
            info = os.stat("/proc/" + str(self.pid) + "/ns/net")
            require((info.st_dev, info.st_ino) == self.network_identity, "Native service has wrong namespace")

    def stop(self):
        if self.closed:
            return
        rows = self.info()
        if rows["LoadState"] != "not-found":
            require(self.invocation is not None and rows.get("InvocationID") == self.invocation,
                    "Retain unproven native unit: " + self.name)
            require(rows.get("MainPID") in {"0", str(self.pid)}, "Retain changed native service PID")
            self.command.run("/usr/bin/systemctl", "stop", self.name)
        if self.pidfd is not None:
            require(select.select([self.pidfd], [], [], 5)[0], "Owned native unit did not stop")
            os.close(self.pidfd)
            self.pidfd = None
        self.closed = True


def wait_for(function, seconds=8):
    deadline = time.monotonic() + seconds
    while True:
        try:
            return function()
        except (NetworkError, FileNotFoundError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.05)


def bus(command, address, *args):
    return command.run("/usr/bin/busctl", "--address=" + address, "--json=short", "--timeout=1s", *args)


def host_dns(command, physical):
    return discover_dns(physical, command)


class HostResolverFixture:
    """Two exact, captured host paths in this disposable guest; no blind restore."""
    def __init__(self, token, service_uid):
        self.token, self.service_uid = token, service_uid
        self.records = []
        self.temporary = []

    def replace(self, path, content, *, link=False):
        path = Path(path)
        info = os.lstat(path)
        uid = 0 if link else self.service_uid
        require(info.st_uid == uid and info.st_nlink == 1 and
                (stat.S_ISLNK(info.st_mode) if link else stat.S_ISREG(info.st_mode)), "Unexpected host resolver fixture source")
        backup = path.parent / (".karyo-qa-" + self.token + "-" + path.name)
        require(not os.path.lexists(backup), "Host DNS fixture backup exists")
        os.link(path, backup, follow_symlinks=False)
        record = {"path":path,"backup":backup,"original":(info.st_dev,info.st_ino,uid),"new":None,"content":content,"link":link}
        self.records.append(record)
        temporary = path.parent / (".karyo-qa-new-" + self.token + "-" + path.name)
        if link:
            os.symlink(content, temporary)
        else:
            fd = os.open(temporary, os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW, 0o644)
            try:
                os.fchown(fd, uid, info.st_gid)
                os.fchmod(fd,0o644)
                require(os.write(fd,content)==len(content),"Incomplete host DNS fixture write")
                os.fsync(fd)
            finally:os.close(fd)
        new=os.lstat(temporary);record["new"]=(new.st_dev,new.st_ino,new.st_uid)
        self.temporary.append((temporary,record["new"]))
        os.replace(temporary,path)

    def close(self):
        for record in reversed(self.records):
            path,backup=record["path"],record["backup"]
            saved=os.lstat(backup)
            require((saved.st_dev,saved.st_ino,saved.st_uid)==record["original"],"Changed host DNS backup retained")
            current=os.lstat(path);identity=(current.st_dev,current.st_ino,current.st_uid)
            if identity==record["original"]:
                backup.unlink()
                continue
            require(identity==record["new"],"Host resolver changed outside this fixture; backup retained")
            current_data=os.readlink(path) if record["link"] else path.read_bytes()
            require(current_data==record["content"],"Changed host DNS fixture payload; backup retained")
            os.replace(backup,path)
        self.records.clear()
        for path,identity in self.temporary:
            if os.path.lexists(path):
                info=os.lstat(path);require((info.st_dev,info.st_ino,info.st_uid)==identity,"Foreign temp resolver file retained")
                path.unlink()
        self.temporary.clear()


CLIENT = r"""import json,os,socket,subprocess,sys,time
from pathlib import Path
stage=Path(sys.argv[1]);private=sys.argv[2];address=sys.argv[3];link=sys.argv[4]
def wait(name):
 end=time.monotonic()+20
 while not (stage/name).exists():
  if time.monotonic()>end:raise RuntimeError('QA client gate timed out')
  time.sleep(.02)
def write(name,data):
 with (stage/name).open('w') as output:json.dump(data,output);output.flush();os.fsync(output.fileno())
wait('public-go')
public=sorted({row[4][0] for row in socket.getaddrinfo('example.org',443,socket.AF_INET,socket.SOCK_STREAM)})
sudo=subprocess.run(['/usr/bin/sudo','-n','/bin/sh','-c','id -u'],capture_output=True,text=True,timeout=3)
host_bus=subprocess.run(['/usr/bin/busctl','--system','--timeout=2s','call','org.freedesktop.login1','/org/freedesktop/login1','org.freedesktop.DBus.Peer','Ping'],capture_output=True,timeout=3)
private_query=subprocess.run(['/usr/bin/busctl','--address='+address,'--timeout=2s','--json=short','get-property','org.freedesktop.resolve1',link,'org.freedesktop.resolve1.Link','DNS'],capture_output=True,timeout=3)
denied=subprocess.run(['/usr/bin/busctl','--address='+address,'--timeout=2s','call','org.freedesktop.resolve1',link,'org.freedesktop.resolve1.Link','SetDefaultRoute','b','false'],capture_output=True,timeout=3)
status=Path('/proc/self/status').read_text()
nnp=next(row.split()[1] for row in status.splitlines() if row.startswith('NoNewPrivs:'))
write('public-result',{'uid':os.getuid(),'public':public,'sudo':sudo.returncode==0 and sudo.stdout.strip()=='0','host_bus':host_bus.returncode==0,'private_query':private_query.returncode==0,'private_change_denied':denied.returncode!=0,'nnp':nnp})
wait('host-file-go')
second=sorted({row[4][0] for row in socket.getaddrinfo('example.net',443,socket.AF_INET,socket.SOCK_STREAM)})
write('host-file-result',{'public_after_host_file':second,'resolver_hash':__import__('hashlib').sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest()})
wait('private-go')
answer=sorted({row[4][0] for row in socket.getaddrinfo(private,443,socket.AF_INET,socket.SOCK_STREAM)})
write('private-result',{'private':answer,'resolver_target':str(Path('/etc/resolv.conf').resolve()),'resolver_hash':__import__('hashlib').sha256(Path('/etc/resolv.conf').read_bytes()).hexdigest()})
wait('exit-go')
"""

SERVER = r"""import socket,struct,sys
from pathlib import Path
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as server:
 server.bind(('127.0.0.99',53));Path(sys.argv[1]).write_text('ready')
 while True:
  query,peer=server.recvfrom(2048);pos=12;labels=[]
  while query[pos]:
   size=query[pos];pos+=1;labels.append(query[pos:pos+size].decode('ascii'));pos+=size
  end=pos+5;name='.'.join(labels)
  yes=name==sys.argv[2] and query[pos+1:pos+5]==struct.pack('!HH',1,1)
  header=query[:2]+struct.pack('!5H',0x8180 if yes else 0x8183,1,1 if yes else 0,0,0)
  answer=b'\xc0\x0c'+struct.pack('!HHIH',1,1,1,4)+socket.inet_aton('192.0.2.123') if yes else b''
  server.sendto(header+query[12:end]+answer,peer)
"""


def snapshot(command):
    resolver = Path("/etc/resolv.conf")
    return {"routes": json.loads(command.run("/usr/sbin/ip", "-j", "-N", "route", "show", "table", "all")),
            "rules": json.loads(command.run("/usr/sbin/ip", "-4", "-j", "-N", "rule", "show")),
            "links": command.run("/usr/sbin/ip", "-brief", "link"),
            "netns": command.run("/usr/sbin/ip", "netns", "list"),
            "firewall": [r for r in json.loads(command.run("/usr/sbin/nft","-j","list","ruleset"))["nftables"] if "metainfo" not in r],
            "forward": Path("/proc/sys/net/ipv4/ip_forward").read_text(),
            "ipv4_conf": {str(p):p.read_text() for p in Path("/proc/sys/net/ipv4/conf").glob("*/*")},
            "resolver_target": str(resolver.resolve()), "resolver_hash": hashlib.sha256(resolver.read_bytes()).hexdigest()}


def main():
    require(os.getuid() == os.geteuid() == 0 and len(sys.argv) == 1, "Root and no arguments required")
    require(socket.gethostname() in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}, "Only disposable QA VMs")
    def interrupted(number, frame):
        raise RuntimeError("Private DNS QA probe interrupted")
    for number in (signal.SIGTERM,signal.SIGHUP,signal.SIGALRM):
        signal.signal(number,interrupted)
    signal.alarm(120)
    command = NetworkCommand()
    before = snapshot(command)
    require(before["forward"].strip() == "0", "Fixture requires initially disabled forwarding")
    physical = discover_underlay(command)
    seed = host_dns(command, physical)
    account = LocalAccounts().account("qaadmin")
    require(account.uid == account.gid == 1000, "Unexpected disposable QA identity")
    store = ConfigStore()
    network = gateway = files = None
    units = []
    stage = None
    stage_files = {}
    evidence = {}
    host_fixture = None
    def tracked_file(name, uid=0):
        fd = os.open(stage / name, os.O_CREAT | os.O_EXCL | os.O_WRONLY | os.O_NOFOLLOW, 0o600 if uid else 0o644)
        os.fchown(fd, uid, uid)
        info = os.fstat(fd);stage_files[name] = (info.st_dev, info.st_ino, uid)
        os.close(fd)
    def read_result(name):
        info = os.lstat(stage / name)
        require((info.st_dev, info.st_ino, info.st_uid) == stage_files[name], "Changed QA result identity")
        value = (stage / name).read_text()
        if not value:
            raise NetworkError("QA result pending")
        return json.loads(value)
    try:
        command.run("/usr/sbin/sysctl", "-qw", "net.ipv4.ip_forward=1")
        with store.locked():
            network = create_network(store, account.uid)
            gateway = ProfileForwarding(network, physical);gateway.create()
            files = ResolverFiles(network, seed);files.create();plan=files.plan
            require(not any(os.path.lexists(p) for p in plan.runtime_paths), "Private resolver runtime already exists")
            stage=Path("/run/karyo-qa-dns-"+network.spec.token);stage.mkdir(mode=0o755)
            unit=Unit(command,plan.bus_name+".service","/usr/bin/dbus-daemon",0)
            units.append(unit);unit.start(plan.bus_properties(),"--nofork","--nopidfile","--nosyslog","--config-file="+plan.config+"/bus.conf")
            wait_for(lambda:bus(command,plan.bus_address,"call","org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus.Peer","Ping"))
            resolver=Unit(command,plan.runtime_name+".service","/usr/lib/systemd/systemd-resolved",plan.service_uid,network.identity)
            units.append(resolver);resolver.start(plan.resolver_properties())
            wait_for(lambda:bus(command,plan.bus_address,"call","org.freedesktop.resolve1","/org/freedesktop/resolve1","org.freedesktop.DBus.Peer","Ping"))
            owner=json.loads(bus(command,plan.bus_address,"call","org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","GetConnectionUnixProcessID","s","org.freedesktop.resolve1"))
            require(owner=={"type":"u","data":[resolver.pid]},"Private resolved bus owner mismatch")
            seed_private_dns(files)
            private_link=json.loads(bus(command,plan.bus_address,"call","org.freedesktop.resolve1","/org/freedesktop/resolve1","org.freedesktop.resolve1.Manager","GetLink","i","2"))["data"][0]
            for name,uid in (("public-result",1000),("host-file-result",1000),("private-result",1000),("mock-ready",0)):
                tracked_file(name,uid)
            private_name="karyo-qa-"+network.spec.token+".test"
            client=Unit(command,"karyo-qa-dns-client-"+network.spec.token+".service","/usr/bin/python3",1000,network.identity)
            units.append(client);client.start(("User=1000","Group=1000",*plan.client_properties()),"-I","-c",CLIENT,str(stage),private_name,plan.bus_address,private_link)
            tracked_file("public-go")
            public=wait_for(lambda:read_result("public-result"))
            require(public["uid"]==1000 and public["public"] and public["sudo"] and public["host_bus"]
                    and public["private_query"] and public["private_change_denied"] and public["nnp"]=="0", "Private libc/sudo/bus client failed")
            host_fixture=HostResolverFixture(network.spec.token,plan.service_uid)
            host_fixture.replace("/run/systemd/resolve/stub-resolv.conf", b"# Karyo QA host-only resolver replacement\nnameserver 192.0.2.254\n")
            tracked_file("host-file-go")
            host_file_result=wait_for(lambda:read_result("host-file-result"))
            require(host_file_result["public_after_host_file"] and host_file_result["resolver_hash"]==hashlib.sha256(Path("/run/"+plan.runtime_name+"/stub-resolv.conf").read_bytes()).hexdigest(),"Client followed host atomic resolver replacement")
            host_fixture.replace("/etc/resolv.conf","/run/systemd/resolve/resolv.conf",link=True)
            old_inode=os.stat("/run/"+plan.runtime_name+"/stub-resolv.conf").st_ino
            mock=Unit(command,"karyo-qa-dns-mock-"+network.spec.token+".service","/usr/bin/python3",0,network.identity)
            units.append(mock);mock.start(("NetworkNamespacePath="+network.path,),"-I","-c",SERVER,str(stage/"mock-ready"),private_name)
            def mock_ready():
                if (stage/"mock-ready").read_text()!="ready":raise NetworkError("Private DNS mock pending")
            wait_for(mock_ready)
            for method,signature,args in (("SetDNS","a(iay)",("1","2","4","127","0","0","99")),
                                           ("SetDomains","a(sb)",("2",".","true","work.test","false")),
                                           ("SetDefaultRoute","b",("true",))):
                bus(command,plan.bus_address,"call","org.freedesktop.resolve1",private_link,"org.freedesktop.resolve1.Link",method,signature,*args)
            tracked_file("private-go")
            private=wait_for(lambda:read_result("private-result"))
            require(private["private"]==["192.0.2.123"] and private["resolver_target"]=="/run/systemd/resolve/resolv.conf"
                    and private["resolver_hash"]==hashlib.sha256(Path("/run/"+plan.runtime_name+"/resolv.conf").read_bytes()).hexdigest(),
                    "Same live libc client did not follow private DNS change/host alias retarget")
            require(os.stat("/run/"+plan.runtime_name+"/stub-resolv.conf").st_ino!=old_inode,"Private aliases did not atomically refresh")
            host_fixture.close()
            require(host_dns(command,physical)==seed,"Host physical DNS changed")
            client.verify();resolver.verify()
            evidence={"public_client":public,"private_client":private,"same_client_pid":client.pid,
                      "private_resolver_pid":resolver.pid,"namespace":network.name,"atomic_alias_refresh":True,
                      "host_atomic_replacement_isolated":True,"host_alias_retarget_isolated":True}
    finally:
        signal.alarm(0)
        errors=[]
        if host_fixture is not None:
            try:host_fixture.close()
            except Exception as error:errors.append(str(error))
        for unit in reversed(units):
            try:unit.stop()
            except Exception as error:errors.append(str(error))
        with store.locked():
            for resource in (files,gateway,network):
                if resource is not None and not resource.closed:
                    try:resource.close()
                    except Exception as error:errors.append(str(error))
        if stage is not None and all(unit.closed for unit in units):
            try:
                require(set(os.listdir(stage))==set(stage_files),"Unknown QA stage file retained")
                for name,identity in stage_files.items():
                    info=os.lstat(stage/name);require((info.st_dev,info.st_ino,info.st_uid)==identity,"Changed QA stage file retained")
                for name in stage_files:(stage/name).unlink()
                stage.rmdir()
            except Exception as error:errors.append(str(error))
        command.run("/usr/sbin/sysctl","-qw","net.ipv4.ip_forward=0")
        require(not errors,"Retained private DNS QA resources: "+"; ".join(errors))
    require(snapshot(command)==before,"Host network or resolver changed")
    print(json.dumps(evidence,indent=2))
    print("PASS private resolved, live libc DNS replacement, client bus/sudo and unchanged host")


if __name__=="__main__":
    main()
