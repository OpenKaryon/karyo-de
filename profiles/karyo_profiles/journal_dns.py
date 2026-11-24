# SPDX-License-Identifier: GPL-3.0-or-later
"""Checkpoint and reattach fixed private DNS resources by native identity."""
from dataclasses import asdict
import os
from pathlib import Path
import stat

from .credentials import LocalAccounts
from .dns import ResolverFiles
from .journal import JournalRetained
from .journal_dns_record import seed,validate_dns
from .journal_network import JournalNetwork,offline,receipt,recover_networks,restore_network
from .logind import Sessions
from .services import ResolverServices,UnitIdentity
from .storage import trusted_directory


def runtime_paths(files,role):
    paths=files.plan.runtime_paths
    return (paths[1],) if role=="bus" else (paths[0],paths[2])


def runtime_identity(path,uid,gid):
    parent=trusted_directory(Path(path).parent)
    try:
        fd=os.open(Path(path).name,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW|os.O_CLOEXEC,dir_fd=parent)
    finally:os.close(parent)
    try:
        info=os.fstat(fd)
        if info.st_uid!=uid or info.st_gid!=gid or stat.S_IMODE(info.st_mode)!=0o750:
            raise JournalRetained("Private DNS runtime directory ownership changed")
        return [info.st_dev,info.st_ino]
    finally:os.close(fd)


def file_receipt(files):
    files.verify()
    return {"directory":list(files.identity),"files":{name:list(value[:2]) for name,value in files.captured.items()}}


def unit_receipt(files,role,unit):
    unit.verify()
    namespace=os.stat("/proc/"+str(unit.identity.pid)+"/ns/net")
    expected=files.network.identity if role=="resolver" else None
    if role=="bus":
        info=os.stat("/proc/1/ns/net");expected=(info.st_dev,info.st_ino)
    if (namespace.st_dev,namespace.st_ino)!=expected:
        raise JournalRetained("Private DNS unit network context changed")
    return {"invocation":unit.identity.invocation,"pid":unit.identity.pid,"start":unit.identity.start,
        "exe":list(unit.executable),"namespace":list(expected),
        "runtime":[runtime_identity(path,unit.uid,files.plan.gid) for path in runtime_paths(files,role)]}


def restore_files(network,dns,accounts):
    files=ResolverFiles(network,seed(dns["seed"]),accounts)
    if files.plan.gid!=dns["gid"] or files.plan.service_uid!=dns["service_uid"]:
        raise JournalRetained("The private DNS service/profile identity changed")
    proof=dns["files"];removing=dns["pending"]=="files-remove"
    try:files.fd=trusted_directory(files.plan.config)
    except FileNotFoundError:
        if not removing or any(os.path.lexists(p) for p in files.plan.runtime_paths):
            raise JournalRetained("Private DNS configuration disappeared unexpectedly")
        files.closed=True
        return files
    try:
        info=os.fstat(files.fd);files.identity=info.st_dev,info.st_ino
        if list(files.identity)!=proof["directory"]:
            raise JournalRetained("Private DNS configuration directory was replaced")
        names=set(os.listdir(files.fd))
        if names-set(proof["files"]) or not removing and names!=set(proof["files"]):
            raise JournalRetained("Private DNS configuration contents changed")
        files.captured={name:(*proof["files"][name],files.plan.files()[name]) for name in names}
        files.verify(complete=not removing)
        return files
    except BaseException:
        os.close(files.fd);files.fd=None
        raise


def process_gone(pid,start):
    try:text=Path("/proc",str(pid),"stat").read_text()
    except (FileNotFoundError,ProcessLookupError):return True
    unused,separator,values=text.rpartition(") ")
    fields=values.split()
    if not separator or len(fields)<20 or not fields[19].isdigit():
        raise JournalRetained("Invalid DNS process exit identity")
    return int(fields[19])!=start


def restore_unit(files,role,unit,proof,removing):
    if proof is None:
        if unit.info()["LoadState"]!="not-found" or any(os.path.lexists(p) for p in runtime_paths(files,role)):
            raise JournalRetained("An unrecorded DNS service or runtime exists")
        unit.closed=True
        return
    unit.identity=UnitIdentity(proof["invocation"],proof["pid"],"/system.slice/"+unit.name,proof["start"])
    unit.executable=tuple(proof["exe"]);unit.attempted=True
    rows=unit.info()
    if removing and process_gone(proof["pid"],proof["start"]):
        if rows["LoadState"]!="not-found" and (rows["Transient"]!="yes" or rows["InvocationID"]!=proof["invocation"]
                or rows["MainPID"]!="0" or rows["ActiveState"] not in {"inactive","failed"}):
            raise JournalRetained("Stopped DNS unit identity changed")
        if any(os.path.lexists(p) for p in runtime_paths(files,role)):
            raise JournalRetained("A stopped DNS service retained runtime objects")
        unit.closed=True
        return
    try:
        unit.pidfd=unit.process.open(proof["pid"])
        if unit_receipt(files,role,unit)!=proof:
            raise JournalRetained("Private DNS service receipt changed")
    except BaseException:
        if unit.pidfd is not None:unit.process.close(unit.pidfd);unit.pidfd=None
        raise


class JournalDNS:
    def __init__(self,journal,network,files,services):
        self.journal,self.network=journal,network
        self.files,self.services=files,services
        self.token=network.token

    def update(self,**values):
        before=self.journal.record(self.token)
        dns=dict(before["dns"],**values)
        self.journal.change(before,dict(before,dns=dns))

    def before_start(self,role):
        self.update(pending=role+"-start")

    def after_start(self,role):
        unit=getattr(self.services,role)
        self.update(pending=None,**{role:unit_receipt(self.files,role,unit)})

    def before_stop(self,role):
        dns=self.journal.record(self.token)["dns"]
        if dns[role]!=unit_receipt(self.files,role,getattr(self.services,role)):
            raise JournalRetained("The captured DNS service changed before shutdown")
        self.update(pending=role+"-stop")

    def after_stop(self,role):
        self.update(pending=None,**{role:None})

    @classmethod
    def start(cls,journal,network,physical_seed,*,accounts=None):
        accounts=LocalAccounts() if accounts is None else accounts
        before=journal.record(network.token)
        if ("dns" in before or before["pending"] is not None or before["network"]!=receipt(network.network)
                or asdict(accounts.by_uid(network.network.spec.uid))!=before["account"]):
            raise JournalRetained("DNS startup requires its captured current network and account")
        files=ResolverFiles(network.network,physical_seed,accounts)
        dns={"seed":asdict(physical_seed),"gid":files.plan.gid,"service_uid":files.plan.service_uid,
             "pending":"files-create","files":None,"bus":None,"resolver":None}
        # asdict preserves tuples; journal publication canonicalizes JSON lists.
        import json
        dns=json.loads(json.dumps(dns))
        journal.change(before,dict(before,dns=dns))
        services=ResolverServices(files);scope=cls(journal,network,files,services)
        try:
            files.create();scope.update(pending=None,files=file_receipt(files))
            services.start(before=scope.before_start,after=scope.after_start)
            return scope
        except BaseException:
            scope.release_handles()
            raise

    @classmethod
    def restore_observers(cls,journal,network,*,accounts=None):
        record=journal.record(network.token);dns=record["dns"]
        validate_dns(dns,network.network.spec,record["account"]["gid"],
                     routing='refresh' in record.get('forwarding',{}))
        if dns["pending"] in {"files-create","bus-start","resolver-start"}:
            raise JournalRetained("An unacknowledged DNS creation requires review")
        files=restore_files(network.network,dns,LocalAccounts() if accounts is None else accounts)
        services=ResolverServices(files);scope=cls(journal,network,files,services)
        try:
            for role in ("bus","resolver"):
                restore_unit(files,role,getattr(services,role),dns[role],dns["pending"]==role+"-stop")
            return scope
        except BaseException:
            scope.release_handles()
            raise

    @classmethod
    def restore(cls,journal,network,*,accounts=None):
        scope=cls.restore_observers(journal,network,accounts=accounts)
        try:
            resolver=scope.services.resolver
            expected=set() if resolver.closed else {str(resolver.identity.pid)}
            consumers=network.network._ip("netns","pids",network.network.name).splitlines()
            if set(consumers)-expected:
                raise JournalRetained("Unknown private DNS namespace consumers remain")
            return scope
        except BaseException:
            scope.release_handles()
            raise

    def close(self):
        if "session" in self.journal.record(self.token) or "provider" in self.journal.record(self.token):
            raise JournalRetained("The profile session and provider must close before private DNS")
        from .dns_refresh import cancel_refresh
        cancel_refresh(self)
        dns=self.journal.record(self.token)["dns"]
        if dns["pending"] in {"files-create","bus-start","resolver-start"}:
            raise JournalRetained("An unacknowledged DNS creation requires review")
        for role in ("resolver","bus"):
            if dns["pending"]==role+"-stop" and getattr(self.services,role).closed:
                self.after_stop(role)
        if not self.files.closed:
            removing=dns["pending"]=="files-remove"
            self.files.verify(complete=not removing)
            self.services.close(before=self.before_stop,after=self.after_stop)
            self.update(pending="files-remove")
            self.files.close()
        before=self.journal.record(self.token)
        updated=dict(before);updated.pop("dns")
        self.journal.change(before,updated)

    def release_handles(self):
        # Detaching observers is never permission to destroy native resources.
        for unit in (self.services.resolver,self.services.bus):
            if unit.pidfd is not None:unit.process.close(unit.pidfd);unit.pidfd=None
        if self.files.fd is not None:os.close(self.files.fd);self.files.fd=None


def recover_profiles(journal,store,*,accounts=None,sessions=None,command=None,exclude=()):
    accounts=LocalAccounts() if accounts is None else accounts
    sessions=Sessions() if sessions is None else sessions
    records=[r for r in journal.snapshot()["profiles"] if r["token"] not in exclude]
    if any("session" in r for r in records):
        raise JournalRetained("A captured session requires its own controller reconciliation")
    from .journal_netbird import recover_providers
    recover_providers(journal, store, accounts=accounts, sessions=sessions, command=command, exclude=exclude)
    records=[r for r in journal.snapshot()["profiles"] if r["token"] not in exclude]
    for record in records:
        if "dns" not in record:continue
        with store.locked():
            offline(accounts.by_uid(record["account"]["uid"]),accounts,sessions)
            if asdict(accounts.by_uid(record["account"]["uid"]))!=record["account"]:
                raise JournalRetained("The journal DNS desktop account changed")
            network=restore_network(store,record,command,require_empty=False)
            if network is None:raise JournalRetained("DNS requires its still-pinned network")
            scope=None
            try:
                wrapped=JournalNetwork(journal,network)
                scope=JournalDNS.restore(journal,wrapped,accounts=accounts)
                scope.close()
                if "forwarding" not in journal.record(wrapped.token):wrapped.close()
            finally:
                if scope is not None:scope.release_handles()
                if network.fd is not None:os.close(network.fd);network.fd=None
    from .journal_forwarding import recover_forwarding
    recover_forwarding(journal,store,accounts=accounts,sessions=sessions,command=command,exclude=exclude)
    recover_networks(journal,store,accounts=accounts,sessions=sessions,command=command,exclude=exclude)
