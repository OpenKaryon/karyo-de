#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Root-only control of a captured public Work for a two-guest exit-node gate."""
import ipaddress
import json
import os
from pathlib import Path
import re
import socket
import stat
import struct
import time
import uuid

ROOT=Path('/var/lib/karyo-qa-netbird-admission')
SOCKET=ROOT/'exit-control.sock'
READY=ROOT/'exit-ready.json'
HOSTS={'karyo-de-acceptance':'debian','karyo-de-ubuntu-acceptance':'ubuntu'}


def require(value,message):
    if not value:raise RuntimeError(message)


def hold(user,cli,status,run,journal,record):
    require(os.getuid()==os.geteuid()==0 and socket.gethostname() in HOSTS,'Fixed root QA context required')
    info=ROOT.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid==info.st_gid==0 and stat.S_IMODE(info.st_mode)==0o700,'Unsafe admission directory')
    directory=(info.st_dev,info.st_ino)
    require(not os.path.lexists(SOCKET) and not os.path.lexists(READY),'Old exit control requires review')
    peer_actor=Path('/usr/local/libexec/karyo-qa-netbird-peers')
    info=peer_actor.lstat()
    require(stat.S_ISREG(info.st_mode) and info.st_uid==0 and not info.st_mode&0o022,'Unsafe payload actor')

    def managed(*args):
        return run(*user('/usr/bin/systemd-run','--user','--wait','--pipe','--quiet','--collect',
            '--unit=karyo-qa-public-exit-'+uuid.uuid4().hex+'.service','--',*args))

    def same_graph():
        rows=journal()['profiles']
        require(len(rows)==1 and rows[0]['token']==record['token'] and rows[0]['provider']==record['provider']
                and rows[0]['session']==record['session'],'Public Work graph changed')
        require(status().get('daemonStatus')=='Connected','Public Work provider disconnected')

    same_graph()
    own=status()['netbirdIp'].split('/')[0]
    require(ipaddress.IPv4Address(own) in ipaddress.IPv4Network('100.64.0.0/10'),'Unexpected overlay address')
    rows=json.loads(managed('/usr/sbin/ip','-4','-j','address','show','dev','eth0'))
    addresses=[a['local'] for row in rows for a in row['addr_info'] if a['family']=='inet']
    require(len(addresses)==1 and ipaddress.IPv4Address(addresses[0]) in ipaddress.IPv4Network('10.231.0.0/16'),'Unexpected routed Work address')
    listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
    proof=None;ready_proof=None
    try:
        listener.bind(str(SOCKET));os.chmod(SOCKET,0o600);listener.listen(1);listener.settimeout(240)
        info=SOCKET.lstat();proof=(info.st_dev,info.st_ino)
        value={'ready':True,'base':HOSTS[socket.gethostname()],'ip':own,'guest':addresses[0],
               'token':record['token'],'provider_invocation':record['provider']['service']['unit']['invocation']}
        fd=os.open(READY,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
        try:os.write(fd,(json.dumps(value)+'\n').encode());os.fsync(fd)
        finally:os.close(fd)
        info=READY.lstat();ready_proof=(info.st_dev,info.st_ino)
        print(json.dumps({'public_exit_control_ready':value['base']}),flush=True)
        while True:
            with listener.accept()[0] as connection:
                pid,uid,gid=struct.unpack('3i',connection.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
                require(uid==0,'Only the explicit root QA controller may request operations')
                connection.settimeout(10)
                with connection.makefile('rb') as stream:packet=stream.readline(4097)
                require(packet.endswith(b'\n') and len(packet)<=4096,'Bounded JSON request required')
                request=json.loads(packet)
                same_graph()
                if request=={'operation':'close'}:
                    connection.sendall(b'{"closing":true}\n');break
                require(set(request)=={'operation','nonce'} and request['operation']=='exit'
                        and isinstance(request['nonce'],str) and re.fullmatch('[0-9a-f]{64}',request['nonce']),'Unsupported exit request')
                deadline=time.monotonic()+35
                while 'karyo-qa-exit' not in cli('networks','list').stdout:
                    require(time.monotonic()<deadline,'Published exit route was not delivered');time.sleep(.2)
                selected=cli('networks','select','karyo-qa-exit')
                require(selected.returncode==0,'Ordinary exit-node selection failed')
                before=json.loads(managed('/usr/sbin/ip','-s','-j','link','show','dev','wt0'))[0]
                route=json.loads(managed('/usr/sbin/ip','-4','-j','route','get','198.18.0.11'))
                result=json.loads(managed('/usr/bin/python3','-I',str(peer_actor),'--exit-client',request['nonce']))
                dns_https=json.loads(managed('/usr/bin/python3','-I','-c',"import ipaddress,json,socket,urllib.request\nanswers=socket.getaddrinfo('api.ipify.org',443,socket.AF_INET,socket.SOCK_STREAM)\nassert answers\nopener=urllib.request.build_opener(urllib.request.ProxyHandler({}))\nwith opener.open('https://api.ipify.org',timeout=15) as reply:data=reply.read(128).decode().strip()\nassert ipaddress.IPv4Address(data).is_global\ntry:socket.getaddrinfo('home-tunnel.karyo.test',443,socket.AF_INET,socket.SOCK_STREAM)\nexcept socket.gaierror:pass\nelse:raise RuntimeError('Home-only DNS name leaked into Work')\nprint(json.dumps({'external_dns':True,'tls_https':True,'public_ipv4_valid':True,'home_private_name_hidden':True}))\n"))
                after=json.loads(managed('/usr/sbin/ip','-s','-j','link','show','dev','wt0'))[0]
                same_graph()
                observation={'base':HOSTS[socket.gethostname()],'route':route,'ordinary':result,'before':before,'after':after,
                             'rules':json.loads(managed('/usr/sbin/ip','-4','-j','-N','rule','show')),'dns_https':dns_https,'public_graph_reused':True}
                connection.sendall((json.dumps({'exit_observation':observation})+'\n').encode())
    finally:
        listener.close()
        require((ROOT.stat().st_dev,ROOT.stat().st_ino)==directory,'Admission directory changed')
        for path,identity,kind in ((SOCKET,proof,stat.S_ISSOCK),(READY,ready_proof,stat.S_ISREG)):
            if identity is not None:
                info=path.lstat()
                require((info.st_dev,info.st_ino)==identity and info.st_uid==0 and kind(info.st_mode)
                        and stat.S_IMODE(info.st_mode)==0o600,'Exit control identity changed')
                path.unlink()


if __name__=='__main__':raise SystemExit('Use only from the captured public Work QA actor')
