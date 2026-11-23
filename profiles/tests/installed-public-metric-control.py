#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed root QA: rank the same physical path without replacing its route."""
from dataclasses import asdict
import hashlib
import json
import os
from pathlib import Path
import socket
import stat
import sys

ROOT=Path('/var/lib/karyo-qa-netbird-admission')
RECEIPT=ROOT/'physical-metric.json'


def require(value,message):
    if not value:raise RuntimeError(message)


def canonical(rows):
    return sorted(json.dumps(row,sort_keys=True,separators=(',',':')) for row in rows)


def check(record,read_journal,managed,dns_code,expect):
    require(os.getuid()==os.geteuid()==0 and socket.gethostname() in
            {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'},'Fixed root QA required')
    require(expect in {'reject','accept'},'Unknown metric expectation')
    info=ROOT.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid==info.st_gid==0
            and stat.S_IMODE(info.st_mode)==0o700,'Unsafe admission directory')
    directory=(info.st_dev,info.st_ino)
    require(not os.path.lexists(RECEIPT),'Retained metric intent requires review')
    sys.dont_write_bytecode=True
    sys.path.insert(0,'/usr/libexec/karyo/profiles')
    from karyo_profiles.journal import validate_record
    from karyo_profiles.journal_forwarding import JournalForwarding
    from karyo_profiles.journal_network import JournalNetwork,restore_network
    from karyo_profiles.networks import NetworkCommand,NetworkError
    from karyo_profiles.storage import ConfigStore
    from karyo_profiles.underlay import discover_underlay

    validate_record(record)
    require(record['pending'] is None and record['forwarding']['pending'] is None,
            'Acknowledged live forwarding required')
    command=NetworkCommand()
    def ip(*args):return command.run('/usr/sbin/ip','-4',*args)
    def routes():return json.loads(ip('-j','route','show','table','main'))
    before=routes();physical=discover_underlay(command)
    require(asdict(physical)==record['forwarding']['physical'],'Initial physical receipt changed')
    metrics={row.get('metric',0) for row in before if row.get('dst')=='default'}
    choices=[m for m in range(physical.metric-1,max(-1,physical.metric-65),-1) if m not in metrics]
    require(choices and choices[0]>=0,'No free preferred metric; no native mutation performed')
    metric=choices[0]
    args=('default','via',physical.gateway,'dev',physical.interface,'src',physical.address,
          'metric',str(metric),'proto','static','table','main')
    intent={'before':before,'physical':asdict(physical),'args':list(args),'metric':metric}
    data=(json.dumps(intent,sort_keys=True)+'\n').encode()
    fd=os.open(RECEIPT,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:os.write(fd,data);os.fsync(fd);info=os.fstat(fd)
    finally:os.close(fd)
    identity=(info.st_dev,info.st_ino)
    owned=None;removed=False;result=None
    try:
        # Keep the OS-owned default and every DHCP/NetworkManager setting.
        # If add is unacknowledged, retain the intent rather than guess ownership.
        ip('route','add',*args)
        after=routes();extra=[row for row in after if row not in before]
        require(len(after)==len(before)+1 and len(extra)==1
                and canonical([row for row in after if row!=extra[0]])==canonical(before),
                'Unexpected native route change; retain metric intent')
        row=extra[0]
        require(row.get('dst')=='default' and row.get('dev')==physical.interface
                and row.get('gateway')==physical.gateway and row.get('prefsrc')==physical.address
                and row.get('metric')==metric and row.get('protocol')=='static'
                and not row.get('flags'),'Unexpected preferred route; retain metric intent')
        owned=row
        selected=discover_underlay(command)
        require(asdict(selected)==dict(asdict(physical),metric=metric),
                'Metric did not select exactly the original physical path')
        require(read_journal()['profiles']==[record],'Live graph changed before verification')

        class ReadOnlyJournal:
            # The real host owns its journal lock. This observer must never
            # acquire that lock or checkpoint/delete the live host's graph.
            def record(self,token):
                require(token==record['token'] and read_journal()['profiles']==[record],
                        'Captured live graph changed')
                return record

        rejected=False
        with ConfigStore().locked() as store:
            network=restore_network(store,record,command,require_empty=False)
            require(network is not None,'Live network disappeared')
            try:
                try:
                    scope=JournalForwarding.restore(ReadOnlyJournal(),JournalNetwork(None,network))
                    scope.verify()
                except NetworkError as error:
                    require(str(error)=='The physical network changed; profile forwarding requires reconciliation',
                            'Unexpected forwarding failure: '+str(error))
                    rejected=True
            finally:
                if network.fd is not None:os.close(network.fd);network.fd=None
        require(rejected==(expect=='reject'),'Installed forwarding did not match the expected before/after behavior')
        dns_https=None if rejected else json.loads(managed('/usr/bin/python3','-I','-c',dns_code))
        require(read_journal()['profiles']==[record],'Metric check replaced a public graph object')
        result={'metric_before':physical.metric,'metric_during':metric,
                'same_selected_path':True,'original_os_routes_retained':True,
                'live_forwarding_rejected':rejected,'public_graph_reused':True,'dns_https':dns_https}
    finally:
        require((ROOT.stat().st_dev,ROOT.stat().st_ino)==directory,'Admission directory changed')
        info=RECEIPT.lstat()
        require((info.st_dev,info.st_ino)==identity and info.st_uid==info.st_gid==0
                and stat.S_IMODE(info.st_mode)==0o600 and info.st_nlink==1
                and RECEIPT.read_bytes()==data,'Metric intent changed')
        if owned is not None:
            current=routes()
            require(canonical(current)==canonical(before+[owned]),'Metric rollback ownership uncertain')
            ip('route','delete',*args)
            require(canonical(routes())==canonical(before),'Original physical routes not restored')
            removed=True
        if removed:
            RECEIPT.unlink()
        else:
            raise RuntimeError('Unacknowledged preferred route; root metric intent retained for review')
    require(discover_underlay(command)==physical,'Original selected path not restored')
    result['original_physical_routes_restored']=True
    return result


if __name__=='__main__':raise SystemExit('Use only from the captured public Work QA controller')
