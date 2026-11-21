#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit disposable-host control of an unprivileged external NetBird lab server."""
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import stat
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import process_gone
from karyo_profiles.services import NativeProcess

ROOT = Path("/var/lib/karyo-qa-netbird-lab")
BINARY = Path("/usr/local/libexec/karyo-qa-netbird-server")
UNIT = "karyo-qa-netbird-server.service"
URL = "http://192.168.230.163:18443"
SHA = "0a1276b42c0112098dbf5a16ed0a908e9f0e641b90c303fd736cbc2f841f6439"


def run(*args):
    return subprocess.check_output(args, text=True, timeout=20).strip()


def private(path, data, uid=0, gid=0):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        os.fchown(fd, uid, gid); os.write(fd, data); os.fsync(fd)
    finally:
        os.close(fd)


def idle():
    assert Path("/proc/sys/net/ipv4/ip_forward").read_text().strip() == "0"
    assert not run("/usr/sbin/ip", "netns", "list"), "Private profile namespaces still exist"
    with RuntimeJournal() as journal:
        assert not journal.snapshot()["profiles"], "Private profile ownership remains"


def request(method, path, data=None, token=None):
    headers = {"Content-Type": "application/json"}
    if token: headers["Authorization"] = "Token " + token
    req = urllib.request.Request(URL + path, data=None if data is None else json.dumps(data).encode(),
                                 headers=headers, method=method)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(req, timeout=8) as response: return json.load(response)
    except urllib.error.HTTPError as error:
        raise RuntimeError("Lab HTTP status " + str(error.code)) from None


def captured():
    info = ROOT.lstat()
    assert stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and info.st_gid == 1000 and info.st_mode & 0o777 == 0o750
    receipt = ROOT / "receipt.json"; info = receipt.lstat()
    assert stat.S_ISREG(info.st_mode) and info.st_uid == 0 and info.st_nlink == 1 and info.st_mode & 0o777 == 0o600
    value = json.loads(receipt.read_text())
    assert value["version"] == 1 and value["unit"] == UNIT and value["url"] == URL and value["pending"] is None
    assert [ROOT.stat().st_dev, ROOT.stat().st_ino] == value["directory"]
    info = BINARY.lstat()
    assert stat.S_ISREG(info.st_mode) and info.st_uid == info.st_gid == 0 and info.st_nlink == 1 and info.st_mode & 0o777 == 0o755
    assert [info.st_dev, info.st_ino] == value["binary"] and value["binary_sha256"] == SHA
    assert hashlib.sha256(BINARY.read_bytes()).hexdigest() == SHA
    service = value["service"]
    assert re.fullmatch("[a-f0-9]{32}", service["invocation"]) and service["cgroup"] == "/system.slice/" + UNIT
    assert type(service["pid"]) is int and service["pid"] > 1 and type(service["start"]) is int and service["start"] > 0
    return value


def info():
    return dict(row.split("=", 1) for row in run("/usr/bin/systemctl", "show", UNIT,
        "-p", "LoadState", "-p", "InvocationID", "-p", "MainPID", "-p", "ActiveState",
        "-p", "ControlGroup", "-p", "Transient").splitlines())


def verify_live(value):
    row, service = info(), value["service"]
    assert row["LoadState"] == "loaded" and row["Transient"] == "yes" and row["ActiveState"] == "active"
    assert row["InvocationID"] == service["invocation"] and row["MainPID"] == str(service["pid"]) and row["ControlGroup"] == service["cgroup"]
    net = os.stat("/proc/1/ns/net")
    assert NativeProcess.read(service["pid"], tuple(value["binary"]), 1000, service["cgroup"],
                              (net.st_dev, net.st_ino)) == service["start"]
    return row


def stop():
    idle(); value = captured(); row = info(); service = value["service"]
    if row["LoadState"] == "not-found":
        assert process_gone(service["pid"], service["start"])
    else:
        verify_live(value)
        run("/usr/bin/systemctl", "--job-mode=fail", "stop", UNIT)
        row = info()
        assert process_gone(service["pid"], service["start"]) and (row["LoadState"] == "not-found" or
            row["InvocationID"] == service["invocation"] and row["MainPID"] == "0" and not row["ControlGroup"]
            and row["ActiveState"] in {"inactive", "failed"} and row["Transient"] == "yes")
    print("PASS captured lab server stopped; no profile namespace or receipt remains", flush=True)
    return value


def cleanup():
    value = stop()
    assert set(p.name for p in ROOT.iterdir()) <= {"server", "receipt.json", "control.json", "setup-key"}
    assert [ROOT.stat().st_dev, ROOT.stat().st_ino] == value["directory"]
    # This exact directory and binary were absent before explicit lab preparation.
    # rmtree uses Linux descriptor-based symlink protection; never follow data links.
    assert shutil.rmtree.avoids_symlink_attacks
    shutil.rmtree(ROOT)
    assert [BINARY.stat().st_dev, BINARY.stat().st_ino] == value["binary"]
    BINARY.unlink()
    print("PASS captured lab directory, temporary credentials and matching binary removed", flush=True)


def prepare():
    idle()
    root=Path('/var/lib/karyo-qa-netbird-lab');binary=Path('/usr/local/libexec/karyo-qa-netbird-server')
    assert not os.path.lexists(root) and not os.path.lexists(binary),'Prior lab objects require review'
    unit='karyo-qa-netbird-server.service'
    def run(*args):return subprocess.check_output(args,text=True,timeout=20).strip()
    assert run('/usr/bin/systemctl','show',unit,'-p','LoadState','--value')=='not-found'
    assert Path('/proc/sys/net/ipv4/ip_forward').read_text().strip()=='0'
    assert not run('/usr/sbin/ip','netns','list')
    for kind,address in ((socket.SOCK_STREAM,('192.168.230.163',18443)),(socket.SOCK_STREAM,('',19090)),
                         (socket.SOCK_STREAM,('127.0.0.1',19000)),(socket.SOCK_DGRAM,('',18478))):
     with socket.socket(socket.AF_INET,kind) as check:
         if kind == socket.SOCK_STREAM: check.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
         check.bind(address)
    staged=Path('/home/qaadmin/netbird-connected-lab-input/netbird-server')
    expected='0a1276b42c0112098dbf5a16ed0a908e9f0e641b90c303fd736cbc2f841f6439'
    assert hashlib.sha256(staged.read_bytes()).hexdigest()==expected
    os.mkdir(root,0o750);os.chown(root,0,1000)
    server=root/'server';os.mkdir(server,0o700);os.chown(server,1000,1000)
    os.mkdir(server/'data',0o700);os.chown(server/'data',1000,1000)
    config=server/'config.yaml'
    base='http://192.168.230.163:18443'
    values={'server':{'listenAddress':'192.168.230.163:18443','exposedAddress':base,
     'stunPorts':[18478],'metricsPort':19090,'healthcheckAddress':'127.0.0.1:19000',
     'logLevel':'warn','logFile':'console','authSecret':secrets.token_urlsafe(32),'dataDir':str(server/'data'),
     'disableAnonymousMetrics':True,'disableGeoliteUpdate':True,
     'auth':{'issuer':base+'/oauth2','signKeyRefreshEnabled':True,
             'sessionCookieEncryptionKey':base64.b64encode(secrets.token_bytes(32)).decode(),
             'dashboardRedirectURIs':[base+'/nb-auth',base+'/nb-silent-auth'],
             'cliRedirectURIs':['http://localhost:53000/']},
     'store':{'engine':'sqlite','encryptionKey':base64.b64encode(secrets.token_bytes(32)).decode()}}}
    private(config,(json.dumps(values)+'\n').encode(),1000,1000)
    subprocess.run(['/usr/bin/install','-o','root','-g','root','-m','755',str(staged),str(binary)],check=True)
    receipt={'version':1,'unit':unit,'pending':'start','directory':[root.stat().st_dev,root.stat().st_ino],
     'binary':[binary.stat().st_dev,binary.stat().st_ino],'binary_sha256':expected,'url':base,'service':None}
    receipt_file=root/'receipt.json';private(receipt_file,(json.dumps(receipt)+'\n').encode())
    properties=['Type=exec','Restart=no','KillMode=control-group','TimeoutStopSec=10s','User=qaadmin','Group=qaadmin',
     'NoNewPrivileges=yes','CapabilityBoundingSet=','ProtectSystem=strict','ProtectHome=yes','PrivateDevices=yes',
     'PrivateTmp=yes','MemoryMax=1G','CPUQuota=100%','WorkingDirectory='+str(server),'ReadWritePaths='+str(server),
     'InaccessiblePaths=-/var/lib/karyo/profile-vpn','Environment=NB_SETUP_PAT_ENABLED=true']
    subprocess.run(['/usr/bin/systemd-run','--quiet','--collect','--unit='+unit,
                    *('--property='+p for p in properties),'--',str(binary),'--config',str(config)],check=True)
    rows=dict(row.split('=',1) for row in run('/usr/bin/systemctl','show',unit,'-p','InvocationID','-p','MainPID','-p','ActiveState','-p','ControlGroup').splitlines())
    assert rows['ActiveState']=='active' and re.fullmatch('[a-f0-9]{32}',rows['InvocationID'])
    pid=int(rows['MainPID']);assert pid>1 and rows['ControlGroup']=='/system.slice/'+unit
    net=os.stat('/proc/1/ns/net');start=NativeProcess.read(pid,tuple(receipt['binary']),1000,rows['ControlGroup'],(net.st_dev,net.st_ino))
    receipt.update(pending=None,service={'invocation':rows['InvocationID'],'pid':pid,'start':start,'cgroup':rows['ControlGroup']})
    receipt_file.write_text(json.dumps(receipt)+'\n')
    deadline=time.monotonic()+120
    while True:
     try:
      request('GET','/api/instance');break
     except (OSError,RuntimeError):
      verify_live(receipt)  # An observation timeout is not a stopped service.
      assert time.monotonic()<deadline,'Lab API readiness timeout'
      time.sleep(.1)
    password='Qa9!'+secrets.token_urlsafe(32)
    owner=request('POST','/api/setup',{'email':'owner@karyo-qa.invalid','name':'Karyo disposable QA','password':password,'create_pat':True,'pat_expire_in':1})
    token=owner.get('personal_access_token');assert isinstance(token,str) and token.startswith('nbp_')
    private(root/'control.json',(json.dumps({'owner':password,'token':token,'url':base})+chr(10)).encode())
    key=request('POST','/api/setup-keys',{'name':'Karyo temporary peer admission','type':'reusable','expires_in':86400,
     'auto_groups':[],'usage_limit':8,'ephemeral':True},token)
    assert isinstance(key.get('key'),str);uuid.UUID(key['key'])
    control=json.loads((root/'control.json').read_text());control['key_id']=key['id'];(root/'control.json').write_text(json.dumps(control)+chr(10))
    private(root/'setup-key',(key['key']+'\n').encode())
    assert NativeProcess.read(pid,tuple(receipt['binary']),1000,rows['ControlGroup'],(net.st_dev,net.st_ino))==start
    print(json.dumps({'lab_server_ready':True,'version':'0.82.1','url':base,'uid':1000,'capabilities':'empty',
     'temporary_owner':True,'temporary_setup_key':True,'public_bootstrap_complete':True}))


if __name__ == "__main__":
    assert os.getuid() == os.geteuid() == 0 and socket.gethostname() == "karyo-de-acceptance"
    if sys.argv[1:] == ["prepare"]: prepare()
    elif sys.argv[1:] == ["stop"]: stop()
    elif sys.argv[1:] == ["cleanup"]: cleanup()
    elif sys.argv[1:] == ["status"]:
        value = captured(); verify_live(value)
        print(json.dumps({"lab_server_ready": True, "version": "0.82.1", "url": URL,
                          "setup_required": request("GET", "/api/instance")["setup_required"]}))
    else: raise SystemExit("Use prepare, status, stop or cleanup on the fixed disposable host")
