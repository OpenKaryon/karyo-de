# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded transport observations for disposable physical-carrier fixtures."""

def udp_exchange(udp, peer, data, expected, budget=30, interval=1):
    """Retry only datagrams; never recreate the fixture's independent TCP stream."""
    import socket
    import time
    started = time.monotonic()
    deadline = started + budget
    attempts = ignored = 0
    while time.monotonic() < deadline:
        udp.sendto(data, peer)
        attempts += 1
        receive_until = min(deadline, time.monotonic() + interval)
        while time.monotonic() < receive_until:
            remaining = receive_until - time.monotonic()
            if remaining <= 0:
                break
            udp.settimeout(remaining)
            try:
                answer, sender = udp.recvfrom(65536)
            except socket.timeout:
                break
            if sender == peer and answer == expected:
                return dict(attempts=attempts, ignored=ignored, seconds=time.monotonic() - started)
            # A previous retransmitted request can reply after the next nonce.
            # It cannot establish success for this request or reset its deadline.
            ignored += 1
    raise TimeoutError('No matching UDP payload within the carrier recovery budget')


def write_networkd_dropin(path, text):
    """Give the ordinary networkd user explicit readable config, regardless of umask."""
    import os
    import stat
    directory = path.parent
    created = not directory.exists()
    if created:
        directory.mkdir(mode=0o755)
        directory.chmod(0o755)
    info = directory.lstat()
    if not stat.S_ISDIR(info.st_mode) or stat.S_IMODE(info.st_mode) & 0o555 != 0o555:
        raise PermissionError('The existing networkd drop-in directory is not readable')
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o644)
    try:
        os.fchmod(descriptor, 0o644)
        with os.fdopen(descriptor, 'w') as stream:
            descriptor = None
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        if descriptor is not None:
            os.close(descriptor)
    info = path.stat()
    return created, (info.st_dev, info.st_ino)


CARRIER_CONTINUITY_CODE = r'''
import hashlib,json,os,socket,subprocess,time
from pathlib import Path
path=Path(PROGRESS);temporary=path.with_suffix('.tmp')
value={'count':0,'errors':0,'one_tcp_connection':True,'stage':'opening',
       'udp_transmits':0,'udp_unmatched_replies':0,'longest_tcp_seconds':0,
       'longest_udp_seconds':0,'longest_dns_seconds':0}
def publish():
 temporary.write_text(json.dumps(value));os.replace(temporary,path)
def stage(name):
 value['stage']=name;publish();return time.monotonic()
def identity(p):
 info=Path(p).stat();return [info.st_dev,info.st_ino]
publish()
try:
 assert identity('/proc/self/ns/net')==NETWORK and identity('/run/systemd/resolve')==RESOLVER
 with socket.create_connection((PEER['ip'],PEER['port']),timeout=30) as stream,socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
  route=json.loads(subprocess.check_output(['/usr/sbin/ip','-j','-4','route','get',PEER['ip']],text=True));assert route[0]['dev']=='wt0'
  end=time.monotonic()+180
  while time.monotonic()<end:
   started=stage('managed-dns')
   values={v[4][0] for v in socket.getaddrinfo(PEER['fqdn'],PEER['port'],socket.AF_INET)};assert values=={PEER['ip']}
   value['longest_dns_seconds']=max(value['longest_dns_seconds'],time.monotonic()-started)
   started=stage('encrypted-tcp')
   data=os.urandom(512);expected=PEER['host'].encode()+b':'+hashlib.sha256(data).digest();stream.sendall(data);answer=b''
   while len(answer)<len(expected):
    block=stream.recv(1024);assert block;answer+=block
   assert answer==expected
   value['longest_tcp_seconds']=max(value['longest_tcp_seconds'],time.monotonic()-started)
   stage('encrypted-udp')
   result=udp_exchange(udp,(PEER['ip'],PEER['port']),data,expected)
   value['udp_transmits']+=result['attempts'];value['udp_unmatched_replies']+=result['ignored']
   value['longest_udp_seconds']=max(value['longest_udp_seconds'],result['seconds'])
   value['count']+=1;value['stage']='completed';publish();time.sleep(.25)
except BaseException as error:
 value.update(errors=1,error=type(error).__name__+': '+str(error)[:180]);publish();raise
'''
