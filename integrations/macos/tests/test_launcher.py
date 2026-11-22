# SPDX-License-Identifier: GPL-3.0-or-later
"""Public CLI controls and ordinary-account credentials, not engine acceptance."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch
from support import ProfileFixture, profiles
from karyo_macos import launcher
from karyo_macos.errors import LaunchError
normal_user=profiles.normal_user


class Credentials(unittest.TestCase):
    def test_real_account_and_all_saved_credentials(self):
        if os.getuid()==0:
            with self.assertRaises(LaunchError):normal_user()
            actual_uid,actual_gid=1001,1001
        else:
            uid,home=normal_user();self.assertEqual(uid,os.getuid());self.assertTrue(home.is_absolute())
            actual_uid,actual_gid=os.getuid(),os.getgid()
        for uids,gids in (((0,0,0),(actual_gid,)*3),
                         ((actual_uid,actual_uid,0),(actual_gid,)*3),
                         ((actual_uid,actual_uid+1,actual_uid),(actual_gid,)*3),
                         ((actual_uid,)*3,(actual_gid,actual_gid,actual_gid+1))):
            with patch.object(profiles.os,'getresuid',return_value=uids),patch.object(profiles.os,'getresgid',return_value=gids):
                with self.assertRaises(LaunchError):normal_user()

    def test_capability_reset_preserves_existing_no_new_privileges(self):
        package=Path(__file__).resolve().parents[1]
        code='import sys;sys.path.insert(0,'+repr(str(package))+');from karyo_macos.launcher import discard_inherited_capabilities;'
        code+='from pathlib import Path;before=Path("/proc/self/status").read_text();discard_inherited_capabilities();after=Path("/proc/self/status").read_text();'
        code+='print(before);print("AFTER");print(after)'
        result=subprocess.check_output([sys.executable,'-I','-B','-c',code],text=True)
        before,after=result.split('AFTER',1)
        def fields(value):return dict(line.split(":",1) for line in value.splitlines() if ":" in line)
        a,b=fields(before),fields(after)
        for key in ('CapInh','CapPrm','CapEff','CapAmb'):self.assertEqual(int(b[key],16),0)
        self.assertEqual(a['NoNewPrivs'],b['NoNewPrivs'])


class Launcher(ProfileFixture, unittest.TestCase):
    def invoke(self,args):
        output,error=io.StringIO(),io.StringIO()
        with patch.object(launcher,'normal_user',return_value=(self.uid,self.home)),contextlib.redirect_stdout(output),contextlib.redirect_stderr(error):
            # The missing-frontend unit case must not depend on whether the
            # machine running the tests also has the optional GUI installed.
            code=launcher.main(args,picker_path=str(self.folder/'missing-picker'))
        return code,output.getvalue(),error.getvalue()

    def test_inspection_is_noncreating_and_does_not_launch_engine(self):
        with patch.object(launcher.shutil,'which',return_value=None):
            code,output,error=self.invoke(['--inspect',str(self.binary)])
        self.assertEqual(code,0,error)
        report=json.loads(output)
        self.assertFalse(report['prefixCreated']);self.assertFalse(report['runtime']['installed'])
        self.assertEqual(report['ownerUid'],self.uid)
        self.assertFalse((self.home/'.karyo-macos').exists())

    def test_status_and_conflicting_modes_do_not_create_user_state(self):
        with patch.object(launcher.shutil,'which',return_value=None):
            code,output,error=self.invoke(['--status','--json'])
            self.assertEqual(code,1);self.assertFalse(json.loads(output)['installed'])
        for args in ([],['--status',str(self.binary)],['--status','--inspect']):
            code,output,error=self.invoke(args);self.assertEqual(code,1);self.assertTrue(error)
        self.assertFalse((self.home/'.karyo-macos').exists())

    def test_status_does_not_equate_available_binary_with_application_pass(self):
        with patch.object(launcher.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'install ok installed\t1.2.3','')):
            status=launcher.runtime_status()
        self.assertTrue(status['installed']);self.assertEqual(status['version'],'1.2.3')
        self.assertEqual(status['applicationCompatibility'],'not implied by engine installation')

    def test_public_launch_refuses_privileged_caller(self):
        with patch.object(launcher,'normal_user',side_effect=LaunchError('ordinary user required')),contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(launcher.main([str(self.binary)]),1)
        self.assertFalse((self.home/'.karyo-macos').exists())

    def test_gui_dispatch_is_literal_and_uses_configured_picker(self):
        picker=self.folder/'picker';picker.write_text('#!/bin/sh\nexit 0\n');picker.chmod(0o700)
        with patch.object(launcher,'normal_user',return_value=(self.uid,self.home)), \
             patch.object(launcher,'discard_inherited_capabilities') as cleared, \
             patch.object(launcher.os,'execv',side_effect=SystemExit(7)) as execute:
            with self.assertRaises(SystemExit):
                launcher.main(['--gui',str(self.binary),'--',"literal '$()'",'one\ntwo'],picker_path=str(picker))
            execute.assert_called_once_with(str(picker),[str(picker),'--',str(self.binary),"literal '$()'",'one\ntwo'])
            cleared.assert_called_once()
        self.assertFalse((self.home/'.karyo-macos').exists())

    def test_gui_modes_reject_conflicts_and_missing_frontend(self):
        with patch.object(launcher,'normal_user',return_value=(self.uid,self.home)),contextlib.redirect_stderr(io.StringIO()):
            for args in (['--gui','--inspect',str(self.binary)],['--status','--gui'],['--gui']):
                self.assertEqual(launcher.main(args,picker_path=str(self.folder/'missing')),1)
        self.assertFalse((self.home/'.karyo-macos').exists())
