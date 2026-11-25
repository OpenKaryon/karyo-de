# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary installed-desktop worker journal and owner-channel-loss probe."""
import json
import os
import time
import uuid

from karyo_profiles.worker_collection import WorkerCollection
from karyo_profiles.worker_context import WorkerContext
from karyo_profiles.worker_declaration import InstalledDeclaration
from karyo_profiles.worker_origin import WorkerOrigin
from karyo_profiles.worker_wire import owner_identity


def probe(context,declaration_path):
    scope = context.scope
    before = scope.journal.record(scope.token)
    assert 'workers' not in before
    original = declaration_path.read_bytes()
    declaration = json.loads(original)
    declaration['argv'].remove('--control-exercise')
    declaration_path.write_text(json.dumps(declaration)+'\n')
    declaration_path.chmod(0o644)
    installed = InstalledDeclaration.capture(declaration['id'])
    result = {}
    completed = False
    original_collection = getattr(scope,'workers',None)
    collection = WorkerCollection(scope)
    scope.workers = collection
    def capture():
        origin = WorkerOrigin(context.origin.bus,context.origin.sender)
        try:return WorkerContext.capture(origin,type('Registry',(),{'store':scope.network.network.store,
            'profiles':{},'main':type('Main',(),{'entries':{scope.token:scope}})()})())
        except BaseException:origin.close();raise
    def drain(owned,owner):
        deadline = time.monotonic()+12
        while owned.live:
            owned.poll(owner)
            assert time.monotonic()<deadline,'Worker collection retirement timed out'
            time.sleep(.05)
    try:
        owner = owner_identity(context.origin.bus,uuid.uuid4().hex)
        instance,descriptor = collection.start(capture(),installed,owner,uuid.uuid4().hex,before_commit=context.verify)
        try:
            final = json.loads(os.read(descriptor,65537))
            assert final['role']=='managed-worker-activation' and final['worker']['id']==instance
        finally:os.close(descriptor)
        assert scope.journal.record(scope.token)['workers'][instance]['phase'] is None
        assert scope.journal.snapshot()['schema']==21
        scope.verify_consumers()
        collection.retire(instance)
        drain(collection,owner)
        assert 'workers' not in scope.journal.record(scope.token)
        result['durable_worker_launch_and_retirement'] = True

        declaration['argv'].append('--leave-routes-on-owner-loss')
        declaration_path.write_text(json.dumps(declaration)+'\n')
        installed = InstalledDeclaration.capture(declaration['id'])
        instance,descriptor = collection.start(capture(),installed,owner,uuid.uuid4().hex,before_commit=context.verify)
        os.close(descriptor)
        unit = collection.live[instance].unit
        pidfd = os.dup(unit.pidfd)
        collection.release_handles()  # Owner EOF; no new activation adopts it.
        import select
        try:assert select.select([pidfd],[],[],5)[0],'Owner EOF did not end its worker'
        finally:os.close(pidfd)
        remaining = installed.declaration.resources.present(scope.network.network)
        assert not remaining['interfaces'] and len(remaining['route_protocols'])==2
        assert remaining['route_tables'] and remaining['rule_tables'] and remaining['nft_tables']
        replacement = scope.workers = WorkerCollection(scope).restore()
        next_owner = owner_identity(context.origin.bus,uuid.uuid4().hex)
        assert next_owner['epoch'] != owner['epoch']
        drain(replacement,next_owner)
        assert 'workers' not in scope.journal.record(scope.token)
        installed.declaration.resources.absent(scope.network.network)
        scope.verify_consumers()
        result['owner_channel_loss_journal_reattachment'] = True
        result['stale_activation_not_republished'] = True
        result['fixed_recovery_entry_removed_surviving_ipv4_ipv6_resources'] = True
        completed = True
        return result
    finally:
        if completed:
            scope.workers = original_collection
            assert json.loads(declaration_path.read_text())==declaration
            declaration_path.write_bytes(original)
