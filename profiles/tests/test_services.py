# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.networks import NetworkSpec
from karyo_profiles.services import (NativeProcess, OwnedUnit, ServiceError,
    ServiceRetained, ResolverServices, UnitIdentity, host_controller)


class Kernel:
    def __init__(self):
        self.rows = dict(LoadState="not-found", MainPID="0", InvocationID="",
                        ControlGroup="", Transient="no", ActiveState="inactive")
        self.calls = []
        self.alive = False
        self.failure = False
        self.start_time = 456
        self.closed = []

    def run(self, *args):
        self.calls.append(args)
        if args[0].endswith("systemd-run"):
            name = next(a.split("=", 1)[1] for a in args if a.startswith("--unit="))
            self.rows.update(LoadState="loaded", MainPID="345", InvocationID="bc"*16,
                ControlGroup="/system.slice/"+name, Transient="yes", ActiveState="active")
            self.alive = True
            if self.failure:
                raise TimeoutError("Unacknowledged native launch")
        elif "stop" in args:
            self.alive = False
            self.rows.update(LoadState="not-found", MainPID="0", InvocationID="",
                ControlGroup="", Transient="no", ActiveState="inactive")
        else:
            return "".join(k+"="+v+"\n" for k, v in self.rows.items())
        return ""

    def open(self, pid):
        return 123

    def read(self, *args):
        return self.start_time

    def exited(self, fd, seconds=0):
        return not self.alive

    def close(self, fd):
        self.closed.append(fd)


class ServicesTest(unittest.TestCase):
    def setUp(self):
        self.kernel = Kernel()
        self.store = SimpleNamespace(_locked=True)
        self.unit = OwnedUnit(self.store, NetworkSpec(1002, "ab"*16), "dns-bus", 0,
            self.kernel, process=self.kernel, host_guard=lambda: None)
        self.patch = patch.object(self.unit, "capture_executable", return_value=(1, 2))
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def start(self):
        return self.unit.start(("User=root", "Group=1002"), "--nofork")

    def stops(self):
        return [a for a in self.kernel.calls if "stop" in a]

    def test_live_identity_and_idempotent_owned_stop(self):
        self.assertIs(self.start(), self.unit)
        self.unit.verify()
        self.unit.close()
        self.unit.close()
        self.assertEqual(len(self.stops()), 1)
        self.assertEqual(self.kernel.closed, [123])

    def test_profile_host_bus_uses_ordinary_account_and_owned_stop(self):
        unit=OwnedUnit(self.store,self.unit.spec,'host-bus',1002,self.kernel,
                       process=self.kernel,host_guard=lambda:None)
        with patch.object(unit,'capture_executable',return_value=(1,2)):
            unit.start(('User=1002','Group=1002','CapabilityBoundingSet=',
                        'RestrictAddressFamilies=AF_UNIX'),'unix:path=/run/dbus/system_bus_socket','/run/fixture/bus')
        unit.verify();unit.close();unit.close()
        self.assertEqual(unit.uid,1002)
        self.assertEqual(len(self.stops()),1)

    def test_collision_is_not_started_or_stopped(self):
        self.kernel.rows["LoadState"] = "loaded"
        with self.assertRaises(ServiceError):
            self.start()
        self.unit.close()
        self.assertFalse(self.stops())
        self.assertFalse(any(a[0].endswith("systemd-run") for a in self.kernel.calls))

    def test_lost_launch_acknowledgement_retains_unknown_service(self):
        self.kernel.failure = True
        with self.assertRaises(ServiceRetained):
            self.start()
        with self.assertRaises(ServiceRetained):
            self.unit.close()
        with self.assertRaises(ServiceRetained):
            self.start()
        self.assertTrue(self.kernel.alive)
        self.assertFalse(self.stops())

    def test_replaced_invocation_pid_cgroup_or_transience_is_retained(self):
        for key, value in (("InvocationID","de"*16),("MainPID","346"),
                           ("ControlGroup","/system.slice/foreign.service"),("Transient","no")):
            with self.subTest(key=key):
                kernel = Kernel()
                unit = OwnedUnit(self.store, self.unit.spec, "dns-bus", 0, kernel,
                    process=kernel, host_guard=lambda: None)
                with patch.object(unit, "capture_executable", return_value=(1,2)):
                    unit.start(("User=root","Group=1002"))
                kernel.rows[key] = value
                with self.assertRaises(ServiceRetained):
                    unit.close()
                self.assertFalse(any("stop" in call for call in kernel.calls))

    def test_pid_reuse_or_exited_process_never_passes_verify(self):
        self.start()
        self.kernel.start_time += 1
        with self.assertRaises(ServiceRetained):
            self.unit.verify()
        with self.assertRaises(ServiceRetained):
            self.unit.close()
        self.assertFalse(self.stops())

    def test_missing_unit_requires_original_process_death(self):
        self.start()
        self.kernel.rows["LoadState"] = "not-found"
        with self.assertRaises(ServiceRetained):
            self.unit.close()
        self.assertFalse(self.stops())
        self.kernel.alive = False
        self.unit.close()
        self.assertTrue(self.unit.closed)

    def test_dead_owned_process_still_needs_captured_invocation(self):
        self.start()
        self.kernel.alive = False
        self.kernel.rows["MainPID"] = "0"
        self.unit.close()
        self.assertEqual(len(self.stops()), 1)

    def test_lock_and_host_context_required_even_for_stop(self):
        self.start()
        self.store._locked = False
        with self.assertRaises(PermissionError):
            self.unit.close()
        self.assertFalse(self.stops())
        self.store._locked = True
        self.unit.host_guard = lambda: (_ for _ in ()).throw(PermissionError("Wrong host"))
        with self.assertRaises(PermissionError):
            self.unit.close()
        self.assertFalse(self.stops())

    def test_user_unit_and_exec_override_properties_refused(self):
        for props in (("User=root","Group=1002","ExecStart=/bin/true"),
                      ("User=root","User=1002","Group=1002"),
                      ("User=root","Group=1002","Environment=X=Y\nUser=root"),
                      ("User=root",)):
            with self.assertRaises(ValueError):
                self.unit.start(props)
        self.assertFalse(self.kernel.calls)

    def test_service_roles_credentials_and_namespace_are_literal(self):
        for role, uid in (("session",0),("dns-bus",1000),("dns-resolver",0),
                          ("dns-resolver",1000),("dns-bus",True)):
            with self.assertRaises(ValueError):
                OwnedUnit(self.store, self.unit.spec, role, uid)
        with self.assertRaises(ValueError):
            OwnedUnit(self.store,self.unit.spec,"dns-resolver",990,network=(1,"2"))

    def test_host_scope_rejects_nonroot_and_private_namespace(self):
        with patch("karyo_profiles.services.os.getuid", return_value=1000):
            with self.assertRaises(PermissionError):
                host_controller()
        with patch("karyo_profiles.services.os.getuid", return_value=0), \
             patch("karyo_profiles.services.os.geteuid", return_value=0), \
             patch("karyo_profiles.services.os.stat", side_effect=[
                 SimpleNamespace(st_dev=1,st_ino=2),SimpleNamespace(st_dev=1,st_ino=3)]):
            with self.assertRaises(PermissionError):
                host_controller()

    def test_native_process_reader_checks_actual_kernel_credentials(self):
        if not Path("/proc/self/stat").exists():
            self.skipTest("Linux procfs")
        pid = os.getpid()
        info = os.stat("/proc/self/exe")
        cgroup = Path("/proc/self/cgroup").read_text().strip()
        if not cgroup.startswith("0::") or "\n" in cgroup:
            self.skipTest("Unified cgroup")
        ns = os.stat("/proc/self/ns/net")
        value = NativeProcess.read(pid,(info.st_dev,info.st_ino),os.getuid(),
            cgroup[3:],(ns.st_dev,ns.st_ino))
        self.assertGreater(value,0)
        with self.assertRaises(ServiceError):
            NativeProcess.read(pid,(info.st_dev,info.st_ino),os.getuid()+1,
                cgroup[3:],(ns.st_dev,ns.st_ino))


class ResolverServicesTest(unittest.TestCase):
    def test_live_namespace_client_prevents_either_service_stop(self):
        kernel = Kernel()
        network = SimpleNamespace(store=SimpleNamespace(_locked=True),
            spec=NetworkSpec(1002,"ab"*16), identity=(1,2), command=kernel,
            name="karyo-"+"ab"*16, verify=lambda:None,
            _ip=lambda *args:"345\\n789\\n")
        files = SimpleNamespace(network=network,plan=SimpleNamespace(service_uid=990))
        services = ResolverServices(files)
        services.resolver.identity = UnitIdentity("bc"*16,345,"/system.slice/fixture",456)
        with self.assertRaises(ServiceRetained):
            services.close()
        self.assertFalse(kernel.calls)
        self.assertFalse(services.closed)
