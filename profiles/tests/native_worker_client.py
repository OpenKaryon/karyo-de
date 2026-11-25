# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary direct caller for the installed worker producer in own native QA."""
import fcntl
import json
import os
from pathlib import Path
import socket
import struct
import sys
import uuid

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

NAME,OBJECT = 'org.karyo.DesktopWorkers1','/org/karyo/DesktopWorkers1'


def main():
    folder = Path(sys.argv[1])
    late = sys.argv[2:] == ['late']
    assert os.getuid() != 0 and folder.parent == Path('/run/user')/str(os.getuid())
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus(private=True)
    daemon = dbus.Interface(bus.get_object('org.freedesktop.DBus','/org/freedesktop/DBus'),'org.freedesktop.DBus')
    owner,busid = str(daemon.GetNameOwner(NAME)),str(daemon.GetId())
    assert int(daemon.GetConnectionUnixUser(owner)) == 0
    interface = dbus.Interface(bus.get_object(NAME,OBJECT,introspect=False),NAME)
    loop = GLib.MainLoop()
    observed = {}
    instance = None
    pending_request = None
    def failed(error,stage='unspecified'):
        observed['error_stage'] = stage
        observed['error'] = error.get_dbus_name() if isinstance(error,dbus.DBusException) else type(error).__name__
        loop.quit()
    def retiring(worker,generation,budget):
        if late:return
        assert str(worker)==instance and int(generation)==1 and 0<int(budget)<=180000
        observed['scoped_retiring_signal'] = True
        GLib.timeout_add(1000,lambda:(loop.quit(),False)[1])
    bus.add_signal_receiver(retiring,signal_name='Retiring',dbus_interface=NAME,bus_name=NAME,path=OBJECT)
    def activated(result):
        nonlocal instance,pending_request
        descriptor = result.take()
        try:
            if late:
                failed(RuntimeError(),'unexpected-late-publication')
                return
            assert fcntl.fcntl(descriptor,fcntl.F_GET_SEALS)&15==15
            value = json.loads(os.read(descriptor,65537))
            assert value['role']=='managed-worker-activation' and value['request']==pending_request
            assert value['owner']['bus_owner']==owner and value['owner']['bus_id']==busid
            assert value['gui']['pid']==os.getpid() and value['gui']['uid']==os.getuid()
            start=Path('/proc/self/stat').read_text().rpartition(') ')[2].split()[19]
            assert value['gui']['start']==start
            if instance is not None:
                assert instance==value['worker']['id']
                observed['fresh_activation_reused_current_worker'] = True
            instance=value['worker']['id']
            endpoint=value['endpoint']
            def node(path):
                info=os.stat(path);return [str(info.st_dev),str(info.st_ino)]
            assert node('/proc/self/ns/net')==value['context']['network']
            assert node(endpoint['path'])==endpoint['node']
            with socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET) as channel:
                channel.settimeout(3)
                channel.connect(endpoint['path'])
                actual=struct.unpack('3i',channel.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
                worker=value['worker']['process']
                assert actual==(worker['pid'],0,worker['gid'])
                assert node(endpoint['path'])==endpoint['node']
                channel.send(json.dumps({'operation':'bootstrap-digest'}).encode())
                reply=channel.recv(4096)
                assert json.loads(reply)=={'prepared_sha256':value['prepared_sha256']}
                observed['exact_fd3_bootstrap_digest'] = True
            observed.update({'sealed_final_record':True,'native_gui_binding':True,'direct_worker_peer':True})
            if not observed.get('fresh_activation_reused_current_worker'):
                pending_request=uuid.uuid4().hex
                interface.Activate('org.karyo.Fixture',pending_request,reply_handler=activated,error_handler=lambda error:failed(error,'activate'),timeout=120)
            else:
                cancelled_request=uuid.uuid4().hex
                def cancelled(error):
                    if error.get_dbus_name()!=NAME+'.Error.Cancelled':failed(error);return
                    observed['pending_cancel_preserved_current_worker'] = True
                    interface.Release(instance,dbus.UInt64(1),reply_handler=lambda:None,error_handler=lambda error:failed(error,'release'),timeout=10)
                interface.Activate('org.karyo.Fixture',cancelled_request,reply_handler=lambda unused:failed(RuntimeError()),error_handler=cancelled,timeout=120)
                interface.Cancel(cancelled_request,reply_handler=lambda:None,error_handler=lambda error:failed(error,'cancel'),timeout=10)
        except Exception as error:failed(error)
        finally:os.close(descriptor)
    def late_cancelled(error):
        if error.get_dbus_name()!=NAME+'.Error.Cancelled':failed(error,'late-activate');return
        observed['late_ready_cancelled'] = True
        loop.quit()
    def wait_cancel():
        if not (folder/'cancel-now').exists():return True
        interface.Cancel(pending_request,reply_handler=lambda:None,error_handler=lambda error:failed(error,'late-cancel'),timeout=10)
        return False
    def begin():
        nonlocal pending_request
        if not (folder/'go').exists():return True
        pending_request=uuid.uuid4().hex
        interface.Activate('org.karyo.Fixture',pending_request,reply_handler=activated,error_handler=late_cancelled if late else lambda error:failed(error,'activate'),timeout=120)
        if late:GLib.timeout_add(25,wait_cancel)
        return False
    (folder/'ready').write_text(json.dumps({'pid':os.getpid()}))
    GLib.timeout_add(50,begin)
    GLib.timeout_add(90000,lambda:(failed(TimeoutError()),False)[1])
    loop.run()
    (folder/'result.json').write_text(json.dumps(observed))
    bus.close()
    assert 'error' not in observed and observed.get('late_ready_cancelled' if late else 'scoped_retiring_signal'),observed


if __name__=='__main__':main()
