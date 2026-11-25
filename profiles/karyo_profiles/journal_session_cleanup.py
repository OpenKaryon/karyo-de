# SPDX-License-Identifier: GPL-3.0-or-later
"""Finish recorded session removal only after its native owners have exited."""
from dataclasses import asdict
import os

from .credentials import LocalAccounts
from .graphical import GraphicalIdentity,GraphicalSession,finish_abandoned_login
from .journal import JournalRetained
from .journal_dns import JournalDNS,process_gone
from .journal_network import JournalNetwork,offline,restore_network
from .networks import NetworkCommand
from .journal_netbird import restore_offline_services
from .journal_host_bus import JournalHostBus
from .journal_session_record import validate_session
from .logind import LoginOwner,Sessions
from .storage import trusted_directory
from .user_manager import BASE_MANAGER,UserManagerScope,manager_exec_start


def closed_graph(manager,value,sessions):
    """A dead process alone does not prove that its logind/unit scope ended."""
    live=value["manager"]["live"]
    if live is not None:
        if not process_gone(live["pid"],live["start"]):
            raise JournalRetained("The recorded user manager is still alive")
        identifiers=sessions._identifiers()
        if any(live[key]["id"] in identifiers for key in ("login","manager_login")):
            raise JournalRetained("A recorded login identifier is still present")
    graph=GraphicalSession(manager);proof=value["graphical"]
    if proof is None:
        if graph.info()["LoadState"]!="not-found" or live is not None and graph.info(live["login"]["scope"])["LoadState"]!="not-found":
            raise JournalRetained("An uncaptured graphical unit remains after close acknowledgement")
        return
    if not process_gone(proof["pid"],proof["start"]):
        raise JournalRetained("The recorded graphical process is still alive")
    for scope,invocation in ((None,proof["invocation"]),(proof["login"]["scope"],proof["scope_invocation"])):
        rows=graph.info(scope)
        if rows["LoadState"]!="not-found" and (rows["InvocationID"]!=invocation
                or rows["Transient"]!="yes" or rows["ControlGroup"]
                or rows["ActiveState"] not in {"inactive","failed"}
                or scope is None and rows["MainPID"]!="0"):
            raise JournalRetained("A recorded graphical unit is not the same completed instance")


def baseline_policy(manager,rows):
    return (manager.dropins(rows)==manager.initial_dropins and not rows["NetworkNamespacePath"]
            and manager_exec_start(manager.command,manager.account.uid)==BASE_MANAGER)


def removing_manager(files,proof,*,allow_missing,netbird=False,catalog=False,host_bus=False,network_client=False):
    """Restore full policy or finish its captured empty/absent removal directory."""
    manager=UserManagerScope(files,netbird=netbird,catalog=catalog,host_bus=host_bus,network_client=network_client)
    manager.initial_dropins=frozenset(proof["dropins"])
    manager.directory_identity=tuple(proof["directory"]);manager.file_identity=tuple(proof["file"])
    manager.created=True
    try:
        rows=manager.info()
        if not manager.inactive(rows) or manager.sessions.owners(manager.account.uid):
            raise JournalRetained("The closing manager is not offline")
        try:manager.fd=trusted_directory(manager.directory)
        except FileNotFoundError:
            if not allow_missing:raise JournalRetained("Manager policy disappeared before removal intent") from None
        if manager.fd is not None:
            info=os.fstat(manager.fd)
            if (info.st_dev,info.st_ino)!=manager.directory_identity:
                raise JournalRetained("The closing manager policy directory was replaced")
            names=set(os.listdir(manager.fd))
            if names=={manager.filename}:
                manager.verify_file();manager.verify_policy(rows)
                return manager
            if names or not allow_missing:
                raise JournalRetained("The closing manager policy has unknown contents")
        if not baseline_policy(manager,rows):manager.verify_policy(rows)
        # The existing close intent and captured inode permit only this empty
        # directory removal. Missing files are never recreated for verification.
        if manager.fd is not None:
            parent=trusted_directory(manager.directory.parent)
            try:
                info=os.stat(manager.directory.name,dir_fd=parent,follow_symlinks=False)
                if (info.st_dev,info.st_ino)!=manager.directory_identity:
                    raise JournalRetained("The closing manager directory changed before removal")
                os.rmdir(manager.directory.name,dir_fd=parent)
            finally:os.close(parent)
        manager.removed=True
        return manager
    except BaseException:
        if manager.fd is not None:os.close(manager.fd);manager.fd=None
        raise


def recover_closed_session(journal,store,record,*,accounts=None,sessions=None):
    value=record["session"]
    if value["pending"] not in {None,"enter","login-close","manager-close"}:return False
    accounts=LocalAccounts() if accounts is None else accounts
    sessions=Sessions() if sessions is None else sessions
    account=accounts.by_uid(record["account"]["uid"])
    if asdict(account)!=record["account"]:raise JournalRetained("The closing journal account changed")
    validate_session(value,account)
    proof=value['graphical']
    if value['pending'] in {None,'enter'}:
        if proof is None or not process_gone(proof['pid'],proof['start']):return False
        # The compositor can exit while the controller is offline or reopening.
        # Record close intent before touching its captured abandoned scope.
        value=dict(value,pending='login-close')
        closing=dict(record,session=value)
        journal.change(record,closing);record=closing
    if value['pending']=='login-close' and proof is not None:
        identity=GraphicalIdentity(proof['invocation'],proof['pid'],proof['start'],
            LoginOwner(**proof['login']),proof['scope_invocation'],
            '/user.slice/user-'+str(account.uid)+'.slice/'+proof['login']['scope'])
        finish_abandoned_login(identity,sessions,NetworkCommand())
    if 'host_network_policy' in record:
        live=value['manager']['live'] if value['manager'] is not None else None
        if sessions.owners(account.uid) or live is not None and not process_gone(live['pid'],live['start']):
            from .user_manager import ManagerPending
            raise ManagerPending('Waiting for the captured child manager before host-network policy retirement')
    elif 'bus_view' not in value:offline(account,accounts,sessions)
    elif sessions.owners(account.uid):
        from .user_manager import ManagerPending
        raise ManagerPending('Waiting for the recorded child login to finish')
    network=restore_network(store,record,require_empty=False)
    if network is None:raise JournalRetained("The closing session lost its namespace")
    dns=manager=provider=network_service=host_bus=None
    try:
        wrapped=JournalNetwork(journal,network)
        dns,provider,network_service=restore_offline_services(journal,wrapped,accounts=accounts)
        if 'host_bus' in record:host_bus=JournalHostBus.restore(journal,dns)
        workers=() if host_bus is None or host_bus.scope.unit.closed else (host_bus.scope.unit,)
        offline(account,accounts,sessions,workers=workers)
        # Check completed graph ownership before removing even an empty policy
        # directory. Constructing this metadata object has no native side effects.
        closed_graph(UserManagerScope(dns.files,netbird=provider is not None,catalog="catalog" in value,
            host_bus='bus_view' in value,network_client=value.get('network_client',False)),value,sessions)
        manager=removing_manager(dns.files,value["manager"],allow_missing=value["pending"]=="manager-close",
            netbird=provider is not None,catalog="catalog" in value,host_bus='bus_view' in value,
            network_client=value.get('network_client',False))
        offline(account,accounts,sessions,workers=workers)
        closing=dict(record,session=dict(value,pending="manager-close",graphical=None))
        journal.change(record,closing)
        if host_bus is not None:host_bus.close(sessions=sessions)
        manager.close()  # Natural exit is already proven; never user@stop.
        if os.path.lexists(manager.directory):
            raise JournalRetained("A manager policy directory appeared after completed removal")
        before=journal.record(record['token']);after=dict(before);after.pop("session")
        journal.change(before,after)
        return True
    finally:
        if provider is not None:provider.release_handles()
        if network_service is not None:network_service.release_handles()
        if host_bus is not None:host_bus.release_handles()
        if manager is not None and manager.fd is not None:os.close(manager.fd);manager.fd=None
        if dns is not None:dns.release_handles()
        if network.fd is not None:os.close(network.fd);network.fd=None
