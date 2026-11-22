# SPDX-License-Identifier: GPL-3.0-or-later
import ctypes,errno,os,select,signal,subprocess,sys,tempfile,time,unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.dont_write_bytecode=True
SOURCE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(SOURCE))
from karyo_vmware import agent,session
from karyo_vmware.children import Child

class FakeChild:
    def __init__(self):self.running=True;self.stopped=False
    def alive(self):return self.running
    def stop(self):self.stopped=True

class AgentPolicyTests(unittest.TestCase):
    def make(self,external=lambda:False):
        self.now=10.;self.children=[]
        def create(*unused):
            child=FakeChild();self.children.append(child);return child
        value=agent.Agent([['vmusr'],['bridge']],clock=lambda:self.now,binder=lambda:os.open('/dev/null',os.O_RDONLY),factory=create,external=external)
        self.addCleanup(value.stop);return value

    def test_background_stops_both_before_the_channel_is_released(self):
        value=self.make();value.tick(True);fd=value.lock
        self.assertEqual(len(self.children),2)
        value.tick(False)
        self.assertTrue(all(child.stopped for child in self.children));self.assertIsNone(value.lock)
        with self.assertRaises(OSError):os.fstat(fd)
        value.tick(True);self.assertEqual(len(self.children),2)
        self.now+=1.5;value.tick(True);self.assertEqual(len(self.children),4)

    def test_foreground_failure_stops_its_sibling_and_uses_bounded_backoff(self):
        value=self.make();value.tick(True);self.children[0].running=False
        value.tick(True);self.assertIsNone(value.lock)
        self.assertTrue(all(child.stopped for child in self.children))
        self.now+=1.;value.tick(True);self.assertEqual(len(self.children),2)
        self.now+=1.;value.tick(True);self.assertEqual(len(self.children),4)
        self.children[-1].running=False;value.tick(True)
        self.assertEqual(value.due,self.now+5)

    def test_existing_external_vmusr_is_respected_without_killing_it(self):
        value=self.make(lambda:True);value.tick(True)
        self.assertFalse(self.children);self.assertIsNone(value.lock)
        self.assertEqual(value.due,self.now+.5)

    def test_another_holder_does_not_start_partial_helpers(self):
        value=self.make();value.binder=lambda:None;value.tick(True)
        self.assertFalse(self.children);self.assertIsNone(value.lock)

    def test_display_worker_shares_foreground_ownership_and_stops_on_lock(self):
        value=self.make();display=FakeChild();value.display=lambda lock:display
        value.tick(True);self.assertIn(display,value.children)
        value.tick(False);self.assertTrue(display.stopped);self.assertIsNone(value.lock)

    def test_non_vmware_exits_without_session_or_lock_operations(self):
        with patch.object(agent.os,'getuid',return_value=1001),patch.object(agent.os,'geteuid',return_value=1001),patch.object(agent,'on_vmware',return_value=False),patch.object(agent,'Session') as lookup,patch.object(sys,'argv',['agent']):
            self.assertEqual(agent.main(),0);lookup.assert_not_called()

    def test_environment_cannot_supply_a_missing_native_session(self):
        with patch.dict(os.environ,{'XDG_SESSION_ID':'1631','USER':'karyoqa'}):
            with self.assertRaises((PermissionError,OSError)):session.Session()

class NativeStatePolicyTests(unittest.TestCase):
    def make(self,*,locked=0,reply=0,**changes):
        value=object.__new__(session.Session);value.key=b'123';value.path=ctypes.c_char_p(b'/test');value.bus=ctypes.c_void_p(1)
        fields=dict(state='active',uid=os.getuid(),vt=7,type='wayland',seat='seat0',**{'class':'user'});fields.update(changes)
        value.text=lambda name:fields[name];value.number=lambda name:fields[name]
        def read(*args):args[-1]._obj.value=locked;return reply
        def connect(pointer):pointer._obj.value=1;return 0
        value.lib=SimpleNamespace(sd_session_is_remote=lambda unused:0,sd_session_is_active=lambda unused:1,sd_bus_get_property_trivial=read,
            sd_bus_unref=lambda unused:None,sd_bus_open_system=connect,sd_bus_set_method_call_timeout=lambda *unused:0)
        return value

    def test_only_unlocked_active_local_graphical_state_is_admitted(self):
        self.assertTrue(self.make().state())
        self.assertFalse(self.make(locked=1).state())
        self.assertFalse(self.make(reply=-errno.ECONNRESET).state())
        self.assertFalse(self.make(state='online').state())
        self.assertIsNone(self.make(state='closing').state())
        for changes in ({'uid':os.getuid()+1},{'type':'tty'},{'seat':'seat1'},{'class':'greeter'},{'vt':0}):
            with self.subTest(changes=changes),self.assertRaises(PermissionError):self.make(**changes).state()

    def test_broken_system_bus_is_reopened_before_foreground_is_readmitted(self):
        value=self.make(reply=-errno.ECONNRESET)
        with patch.object(value.lib,'sd_bus_open_system',wraps=value.lib.sd_bus_open_system) as reopen:
            self.assertFalse(value.state());reopen.assert_called_once()
            value.lib.sd_bus_get_property_trivial=lambda *unused:0
            self.assertTrue(value.state());reopen.assert_called_once()

@unittest.skipUnless(os.name=='posix' and os.getuid()==1001 and Path('/run/karyo-vmware/channel').exists(),'Needs isolated ordinary-UID channel fixture')
class OwnedChildrenTests(unittest.TestCase):
    def setUp(self):
        self.fd=agent.acquire();self.assertIsNotNone(self.fd);self.addCleanup(os.close,self.fd)
        self.root=Path(tempfile.mkdtemp());self.addCleanup(lambda:self.root.rmdir())
        self.addCleanup(self.clean)
        self.workers=[]

    def clean(self):
        for child in self.workers:child.stop()
        for p in self.root.iterdir():p.unlink()

    def test_worker_is_foreground_unprivileged_and_cleanup_releases_its_group(self):
        ready=self.root/'status'
        code="import pathlib,time;pathlib.Path(__import__('sys').argv[1]).write_text(pathlib.Path('/proc/self/status').read_text());time.sleep(30)"
        child=Child(['/usr/bin/python3','-I','-c',code,str(ready)],self.fd);self.workers.append(child)
        deadline=time.monotonic()+5
        while not ready.exists() and time.monotonic()<deadline:time.sleep(.03)
        self.assertTrue(ready.exists());self.assertTrue(child.alive())
        fields=dict(line.split(':',1) for line in ready.read_text().splitlines() if ':' in line)
        self.assertEqual(fields['NoNewPrivs'].strip(),'1')
        for name in ('CapEff','CapPrm','CapInh','CapAmb'):self.assertEqual(int(fields[name],16),0)
        self.assertIsNone(agent.acquire())
        child.stop();self.assertFalse(child.live_group());self.assertIsNotNone(child.process.returncode)

    def test_exited_leader_remains_pinned_until_its_descendant_is_stopped(self):
        child=Child(['/usr/bin/python3','-I','-c',"import os,time;pid=os.fork();time.sleep(30) if pid==0 else None"],self.fd)
        self.workers.append(child);deadline=time.monotonic()+5
        while child.alive() and time.monotonic()<deadline:time.sleep(.03)
        self.assertFalse(child.alive());self.assertIsNone(child.process.returncode)
        self.assertTrue(child.live_group());child.stop();self.assertFalse(child.live_group())

    def test_parent_death_terminates_its_actual_foreground_worker(self):
        marker=self.root/'worker-ready';pidfile=self.root/'worker-pid'
        payload="import pathlib,time,sys;pathlib.Path(sys.argv[1]).write_text('ready');time.sleep(30)"
        launcher=("import sys,time,pathlib;sys.dont_write_bytecode=True;sys.path.insert(0,"+repr(str(SOURCE))+");"
            "from karyo_vmware.children import Child;child=Child(['/usr/bin/python3','-I','-c',"+repr(payload)+","+repr(str(marker))+"],int(sys.argv[1]));"
            "pathlib.Path(sys.argv[2]).write_text(str(child.process.pid));time.sleep(30)")
        parent=subprocess.Popen([sys.executable,'-I','-c',launcher,str(self.fd),str(pidfile)],pass_fds=(self.fd,),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        handle=None
        try:
            deadline=time.monotonic()+5
            while (not marker.exists() or not pidfile.exists()) and time.monotonic()<deadline:time.sleep(.03)
            self.assertTrue(marker.exists());handle=os.pidfd_open(int(pidfile.read_text()))
            parent.kill();parent.wait(timeout=3)
            self.assertTrue(select.select([handle],[],[],5)[0],'Worker survived its parent')
        finally:
            if parent.poll() is None:parent.kill();parent.wait(timeout=3)
            if handle is not None:
                if not select.select([handle],[],[],0)[0]:signal.pidfd_send_signal(handle,signal.SIGKILL)
                os.close(handle)
