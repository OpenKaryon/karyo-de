# SPDX-License-Identifier: GPL-3.0-or-later
"""Direct native caller activation inside the existing root profile owner."""
import os
import sys
from pathlib import Path
import time
import uuid

import dbus
import dbus.service
from dbus.lowlevel import SignalMessage
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

from .services import host_controller
from .worker_context import WorkerContext
from .worker_declaration import InstalledDeclaration
from .worker_errors import WorkerPending, WorkerRetained
from .worker_origin import WorkerOrigin
from .worker_record import opaque
from .worker_wire import owner_identity

NAME = 'org.karyo.DesktopWorkers1'
PATH = '/org/karyo/DesktopWorkers1'
ACTION = 'org.karyo.desktops.worker'
POLICY = Path('/usr/share/dbus-1/system.d/org.karyo.DesktopWorkers1.conf')


def failure(suffix):
    kind = type(suffix,(dbus.DBusException,),{'_dbus_error_name':NAME+'.Error.'+suffix})
    return kind('The desktop worker operation could not finish: '+suffix)


def fixed_owner(bus):
    host_controller()
    expected = '/system.slice/karyo-profile-host.service'
    if Path('/proc/self/cgroup').read_text().splitlines() != ['0::'+expected]:
        raise PermissionError('Worker activation requires its fixed profile owner service')
    manager = dbus.Interface(bus.get_object('org.freedesktop.systemd1','/org/freedesktop/systemd1'),
                             'org.freedesktop.systemd1.Manager')
    obj = manager.GetUnit('karyo-profile-host.service',timeout=2)
    properties = dbus.Interface(bus.get_object('org.freedesktop.systemd1',obj),'org.freedesktop.DBus.Properties')
    values = properties.GetAll('org.freedesktop.systemd1.Service',timeout=2)
    if (int(values['MainPID']) != os.getpid() or str(values['ControlGroup']) != expected
            or values['PrivateNetwork'] or str(values['NetworkNamespacePath'])):
        raise PermissionError('The worker owner native service identity changed')


class DesktopWorkers(dbus.service.Object):
    def __init__(self,bus,registry):
        fixed_owner(bus)
        self.bus,self.registry = bus,registry
        self.requests,self.queue,self.binding = {},[],[]
        self.name = dbus.service.BusName(NAME,bus=bus,do_not_queue=True)
        self.owner = owner_identity(bus,uuid.uuid4().hex)
        # Synchronous credential/property reads must not share the connection
        # dispatching Activate/Cancel. A nested libdbus read can otherwise leave
        # incoming service calls queued behind the very handler doing the read.
        self.lookup = dbus.bus.BusConnection('unix:path=/run/dbus/system_bus_socket')
        daemon = dbus.Interface(self.lookup.get_object('org.freedesktop.DBus',
            '/org/freedesktop/DBus',introspect=False),'org.freedesktop.DBus')
        if str(daemon.GetId(timeout=2)) != self.owner['bus_id']:
            self.lookup.close()
            raise PermissionError('Worker inspection must use the same native system bus')
        self.lost = False
        self.watched = set()
        super().__init__(self.name,PATH)
        bus.add_signal_receiver(self.name_changed,signal_name='NameOwnerChanged',
                                dbus_interface='org.freedesktop.DBus',arg0=NAME)
        self.authority = dbus.Interface(self.lookup.get_object('org.freedesktop.PolicyKit1',
            '/org/freedesktop/PolicyKit1/Authority'),'org.freedesktop.PolicyKit1.Authority')

    def pump(self):
        context = GLib.MainContext.default()
        for unused in range(32):
            if not context.pending():break
            context.iteration(False)
        if self.lost or not self.bus.get_is_connected():
            raise WorkerRetained('The current worker producer bus lifetime ended')

    def trace(self,request,stage,created):
        print('karyo-profile-host: worker '+request[:8]+' '+stage+' elapsed='+format(time.monotonic()-created,'.3f'),file=sys.stderr,flush=True)

    def finish_error(self,value,suffix):
        self.trace(value['request'],'failed-'+suffix,value['created'])
        if value['pending']:
            value['pending'] = False
            value['error'](failure(suffix))
        value['reply'] = value['error'] = None
        if value['instance'] is None and not value['starting']:
            value['origin'].close()

    def current(self,value):
        if value['cancelled']:raise failure('Cancelled')
        if time.monotonic() >= value['deadline']:raise failure('Unavailable')
        if self.lost:raise failure('Unavailable')
        value['origin'].verify()

    @dbus.service.method(NAME,in_signature='ss',out_signature='h',sender_keyword='sender',
                         async_callbacks=('reply','error'))
    def Activate(self,declaration,request,reply,error,sender=None):
        origin = None
        created = time.monotonic()
        self.trace(str(request),'received',created)
        try:
            opaque(str(request))
            key = str(sender),str(request)
            if key in self.requests or len(self.requests) >= 256:raise WorkerPending('Request already used or owner queue full')
            origin = WorkerOrigin(self.lookup,str(sender))
            if str(sender) not in self.watched:
                self.bus.add_signal_receiver(self.name_changed,signal_name='NameOwnerChanged',
                    dbus_interface='org.freedesktop.DBus',arg0=str(sender))
                self.watched.add(str(sender))
            self.requests[key] = {'created':created,'origin':origin,'context':None,
                'declaration_id':str(declaration),'declaration':None,'request':str(request),
                'pending':True,'cancelled':False,'starting':False,'instance':None,'release':False,
                'deadline':created+120,'reply':reply,'error':error,'auth':None}
            self.binding.append(key)
            self.trace(str(request),'origin-captured',created)
        except Exception as exc:
            if origin is not None:origin.close()
            suffix = 'Busy' if isinstance(exc,WorkerPending) else 'Unavailable'
            self.trace(str(request),'capture-failed-'+suffix,created)
            error(failure(suffix))

    def bind(self,key,value):
        self.current(value)
        context = WorkerContext.capture(value['origin'],self.registry)
        record = context.scope.journal.record(context.scope.token)
        if 'host_network_policy' not in record or 'host_bus' in record:
            raise failure('Unsupported')
        value['context'] = context
        value['declaration'] = InstalledDeclaration.capture(value['declaration_id'])
        value['auth'] = uuid.uuid4().hex
        self.trace(value['request'],'desktop-captured',value['created'])
        subject = dbus.Struct(('system-bus-name',dbus.Dictionary({'name':dbus.String(key[0])},signature='sv')),
                              signature='sa{sv}')
        def checked(result):
            self.trace(value['request'],'authorization-reply',value['created'])
            value['auth'] = None
            try:
                self.current(value)
                if not bool(result[0]):raise failure('Unauthorized')
                self.queue.append(key)
                self.trace(value['request'],'queued',value['created'])
            except Exception:self.finish_error(value,'Cancelled' if value['cancelled'] else 'Unauthorized')
        self.trace(value['request'],'authorization-request',value['created'])
        self.authority.CheckAuthorization(subject,ACTION,dbus.Dictionary({},signature='ss'),
            dbus.UInt32(1),value['auth'],reply_handler=checked,
            error_handler=lambda unused:self.finish_error(value,'Cancelled' if value['cancelled'] else 'Unavailable'),
            timeout=max(1,value['deadline']-time.monotonic()))

    def cancel(self,value):
        value['cancelled'] = True
        value['release'] = True
        if value['auth'] is not None:
            self.authority.CancelCheckAuthorization(value['auth'],reply_handler=lambda:None,error_handler=lambda unused:None,timeout=2)
            value['auth'] = None
        self.finish_error(value,'Cancelled')

    @dbus.service.method(NAME,in_signature='s',out_signature='',sender_keyword='sender')
    def Cancel(self,request,sender=None):
        self.trace(str(request),'cancel-received',time.monotonic())
        opaque(str(request))
        value = self.requests.get((str(sender),str(request)))
        if value is not None and value['pending']:self.cancel(value)
        self.trace(str(request),'cancel-return',time.monotonic())

    @dbus.service.method(NAME,in_signature='st',out_signature='',sender_keyword='sender')
    def Release(self,instance,generation,sender=None):
        self.trace(str(instance),'release-received',time.monotonic())
        opaque(str(instance))
        if int(generation) != 1:raise failure('Unavailable')
        matches = [value for key,value in self.requests.items() if key[0] == str(sender) and value['instance'] == str(instance)]
        if not matches:raise failure('Unavailable')
        value = matches[-1]
        try:self.current(dict(value,deadline=time.monotonic()+1))
        except Exception:raise failure('Unavailable') from None
        value['release'] = True
        self.trace(str(instance),'release-queued',time.monotonic())

    @dbus.service.signal(NAME,signature='stu')
    def Retiring(self,instance,generation,remaining_budget_ms):
        # Actual retirement uses a destination-addressed low-level signal.
        pass

    def notify(self,instance,value,budget):
        self.trace(instance,'retiring-signal',time.monotonic())
        if value['source']['bus'] != self.owner['bus_id']:return
        signal = SignalMessage(PATH,NAME,'Retiring')
        signal.set_destination(value['source']['sender'])
        signal.append(instance,dbus.UInt64(1),dbus.UInt32(budget),signature='stu')
        self.bus.send_message(signal)

    def name_changed(self,name,old,new):
        if str(name) == NAME and str(old) == self.owner['bus_owner'] and str(new) != self.owner['bus_owner']:
            self.lost = True
        if str(name).startswith(':') and not new:
            for key,value in tuple(self.requests.items()):
                if key[0] == str(name):
                    if value['pending']:self.cancel(value)
                    else:value['release'] = True

    def scopes(self):
        return list(self.registry.profiles.values())+list(self.registry.main.entries.values())

    def poll(self):
        self.pump()
        with self.registry.store.locked():
            for scope in self.scopes():
                scope.workers.owner = self.owner
                scope.workers.notify = self.notify
                for value in tuple(self.requests.values()):
                    if value['context'] is not None and value['context'].scope is scope and value['release'] and value['instance'] in scope.workers.live:
                        scope.workers.retire(value['instance'])
                scope.workers.poll(self.owner)
            if self.binding:
                key = self.binding.pop(0)
                value = self.requests[key]
                if value['pending'] and not value['cancelled']:
                    try:self.bind(key,value)
                    except Exception:self.finish_error(value,'Cancelled' if value['cancelled'] else 'Unavailable')
            for value in tuple(self.requests.values()):
                if value['pending'] and time.monotonic() >= value['deadline']:self.cancel(value)
            while self.queue:
                key = self.queue.pop(0)
                value = self.requests[key]
                if not value['pending'] or value['cancelled']:continue
                descriptor = None
                value['starting'] = True
                try:
                    self.trace(value['request'],'launch',value['created'])
                    def before_commit():
                        self.trace(value['request'],'revalidate',value['created'])
                        self.pump()
                        self.current(value)
                    before_commit()
                    context = value['context']
                    collection = context.scope.workers
                    instance,descriptor = collection.start(context,value['declaration'],self.owner,value['request'],before_commit=before_commit,progress=lambda stage:self.trace(value['request'],stage,value['created']))
                    value['instance'] = instance
                    self.current(value)
                    self.trace(value['request'],'return-activation',value['created'])
                    value['reply'](dbus.types.UnixFd(descriptor))
                    value['pending'] = False
                    value['reply'] = value['error'] = None
                except Exception as exc:
                    suffix = 'Cancelled' if value['cancelled'] else 'Busy' if isinstance(exc,WorkerPending) else 'Retained' if isinstance(exc,WorkerRetained) else 'Unavailable'
                    self.finish_error(value,suffix)
                finally:
                    value['starting'] = False
                    if not any(item.context is value['context'] for item in value['context'].scope.workers.live.values()):
                        value['origin'].close()
                    if descriptor is not None:os.close(descriptor)
                break  # Bounded native launch work per owner iteration.

    def serve(self,listener,dispatcher,registry):
        # D-Bus watches must participate in an actual GLib poll cycle. A loop
        # which only peeks context.pending() alongside select(listener) can
        # leave already queued method calls undispatched until another bus IO.
        loop = GLib.MainLoop()
        connections,errors = [],[]
        def incoming(unused,condition):
            if condition & (GLib.IO_ERR|GLib.IO_HUP):
                errors.append(OSError('The desktop host listener closed'))
                loop.quit()
                return False
            connection,unused = listener.accept()
            if len(connections) >= 16:connection.close()
            else:connections.append(connection)
            return True
        def tick():
            try:
                self.poll()
                registry.poll()
                if connections:
                    with connections.pop(0) as connection:dispatcher.handle(connection)
            except BaseException as exc:
                errors.append(exc)
                loop.quit()
                return False
            return True
        watch = GLib.io_add_watch(listener.fileno(),GLib.IO_IN|GLib.IO_ERR|GLib.IO_HUP,incoming)
        timer = GLib.timeout_add(250,tick)
        try:
            loop.run()
            if errors:raise errors[0]
        finally:
            for identifier in (watch,timer):
                source = GLib.MainContext.default().find_source_by_id(identifier)
                if source is not None:source.destroy()
            for connection in connections:connection.close()

    def close(self):
        self.remove_from_connection()
        self.lookup.close()
        self.bus.close()


def installed(registry):
    if not POLICY.exists():return None
    info = POLICY.stat()
    if info.st_uid != 0 or info.st_mode & 0o022:
        raise PermissionError('The installed worker bus policy changed')
    DBusGMainLoop(set_as_default=True)
    bus = dbus.bus.BusConnection('unix:path=/run/dbus/system_bus_socket')
    try:return DesktopWorkers(bus,registry)
    except BaseException:bus.close();raise
