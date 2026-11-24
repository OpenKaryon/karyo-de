#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A fresh process observes only acknowledged workers in the native QA fixture."""
import json
import os
from pathlib import Path
import socket
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.journal import RuntimeJournal
from karyo_profiles.journal_dns import JournalDNS
from karyo_profiles.journal_network import JournalNetwork, restore_network
from karyo_profiles.journal_network_service import JournalNetworkService
from karyo_profiles.storage import ConfigStore


def restore_context(journal, store, root, record, accounts):
    network = JournalNetwork(journal, restore_network(store, record, require_empty=False))
    dns = service = None
    try:
        dns = JournalDNS.restore_observers(journal, network, accounts=accounts)
        service = JournalNetworkService.restore(journal, dns, state_root=root/'state')
        service.verify()
        members = service.consumer_pids()
        allowed = members | {dns.services.resolver.identity.pid}
        actual = {int(value) for value in network.network._ip('netns', 'pids', network.network.name).splitlines()}
        if actual != allowed:raise RuntimeError('The native fixture has unknown namespace consumers')
        return network, dns, service
    except BaseException:
        if service is not None:service.release_handles()
        if dns is not None:dns.release_handles()
        if network.network.fd is not None:os.close(network.network.fd);network.network.fd = None
        raise


def main():
    retiring = len(sys.argv) == 3 and sys.argv[2] == '--retire'
    if (os.getuid() != os.geteuid() or os.geteuid() != 0 or (len(sys.argv) != 2 and not retiring)
            or socket.gethostname() not in ('karyo-network-debian', 'karyo-network-ubuntu')):
        raise RuntimeError('Use the independent native QA fixture')
    root = Path(sys.argv[1]).resolve(strict=True)
    if root.parent != Path('/var/lib/karyo-qa') or not root.name.startswith('network-manager-'):
        raise RuntimeError('Invalid native QA fixture root')
    store = ConfigStore(root/'config', root/'no-legacy', root/'runtime')
    accounts, evidence = LocalAccounts(), []
    with RuntimeJournal(root/'journal') as journal:
        if retiring:
            from karyo_profiles.journal_dns import recover_profiles
            tokens = [record['token'] for record in journal.snapshot()['profiles']]
            recover_profiles(journal, store, accounts=accounts, network_service_state_root=root/'state')
            if journal.snapshot()['profiles']:raise RuntimeError('Generic runtime recovery retained owned fixture resources')
            print(json.dumps({'retired': tokens}))
            return
        with store.locked():
            for record in journal.snapshot()['profiles']:
                network, dns, service = restore_context(journal, store, root, record, accounts)
                try:
                    evidence.append({'token': record['token'], 'invocation': service.scope.unit.identity.invocation,
                                     'pid': service.scope.unit.identity.pid, 'cgroup_members': len(service.consumer_pids())})
                finally:
                    service.release_handles();dns.release_handles()
                    os.close(network.network.fd);network.network.fd = None
    print(json.dumps(evidence))


if __name__ == '__main__':main()
