# SPDX-License-Identifier: GPL-3.0-or-later
"""Account and storage contracts, including real fixed-shell argument forwarding."""
import json
import os
from pathlib import Path
import plistlib
import stat
import subprocess
import sys
import unittest
from unittest.mock import patch
from support import ProfileFixture, profiles
from karyo_macos.errors import LaunchError


class Profiles(ProfileFixture, unittest.TestCase):
    def test_account_home_and_slot_spoofing_cannot_select_other_profile(self):
        first = self.plan({'DISPLAY': ':1', 'HOME': '/other', 'USER': 'root', 'LOGNAME': 'root', 'OPENKARYON_SESSION_SLOT': '1'})
        second = self.plan({'DISPLAY': ':1', 'HOME': '/another', 'OPENKARYON_SESSION_SLOT': '2', 'XDG_SESSION_ID': 'other'})
        self.assertEqual(first.prefix, second.prefix)
        self.assertTrue(first.prefix.is_relative_to(self.home))
        self.assertEqual(first.environment['HOME'], str(self.home))
        import pwd
        self.assertEqual(first.environment['USER'], pwd.getpwuid(self.uid).pw_name)
        self.assertNotEqual(first.prefix, self.plan({'DISPLAY': ':2'}).prefix)

    def test_preview_does_not_create_state_or_require_engine(self):
        with patch.object(profiles.shutil, 'which', return_value=None):
            plan = self.plan(create=False)
            self.assertFalse((self.home/'.karyo-macos').exists())
            self.assertIsNotNone(plan.compatibility_profile)
            with self.assertRaisesRegex(LaunchError, 'Darling не установлен'): self.plan()
        self.assertFalse((self.home/'.karyo-macos').exists())

    def test_profile_contents_modes_and_existing_data_survive(self):
        plan = self.plan(); profile = plan.compatibility_profile
        self.assertEqual(plistlib.loads(profile.read_bytes())['ProductVersion'], '10.13.6')
        self.assertEqual(stat.S_IMODE(profile.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(profile.parent.stat().st_mode), 0o700)
        self.assertTrue(plan.persistent)
        self.assertFalse(plan.prefix.exists())
        plan.prefix.mkdir(); data = plan.prefix/'saved'; data.write_bytes(b'keep')
        inode = profile.stat().st_ino
        self.assertEqual(self.plan().compatibility_profile.stat().st_ino, inode)
        profile.write_bytes(b'stale'); profile.chmod(0o644)
        self.plan()
        self.assertEqual(data.read_bytes(), b'keep')
        self.assertEqual(profile.read_bytes(), profiles.GRAPHICAL_SYSTEM_VERSION)

    def test_symlink_prefix_ancestor_and_metadata_hardlinks_are_refused(self):
        plan = self.plan(); plan.prefix.symlink_to(self.home, target_is_directory=True)
        with self.assertRaises(OSError): self.plan()
        plan.prefix.unlink()
        profile = plan.compatibility_profile; copy = self.folder/'metadata-copy'
        os.link(profile, copy)
        with self.assertRaisesRegex(LaunchError, 'ссылок'): self.plan()
        copy.unlink(); profile.unlink(); profile.symlink_to(self.binary)
        with self.assertRaises(OSError): self.plan()
        profile.unlink()
        import shutil
        base = self.home/'.karyo-macos'; shutil.rmtree(base)
        outside = self.folder/'outside'; outside.mkdir(); base.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(OSError): self.plan()
        self.assertEqual(list(outside.iterdir()), [])

    def test_foreign_directory_owner_is_rejected(self):
        plan = self.plan()
        with self.assertRaisesRegex(LaunchError, 'пользователю'):
            profiles.open_private_directory(plan.prefix.parent, self.uid+1)

    def test_socket_path_boundary_in_utf8_bytes_before_writes(self):
        plan = self.plan(create=False)
        suffix = len(os.fsencode(plan.prefix))-len(os.fsencode(self.home))
        for initial in ('a', 'ж'):
            for size in (79,80):
                remaining = size-suffix-len(os.fsencode(self.folder))-1
                home = self.folder/(initial+'a'*(remaining-len(initial.encode())))
                home.mkdir()
                with patch.object(profiles, 'normal_user', return_value=(self.uid,home)):
                    if size==79: self.assertEqual(len(os.fsencode(self.plan().prefix)),79)
                    else:
                        with self.assertRaisesRegex(LaunchError,'79 байт UTF-8'):self.plan()
                        self.assertEqual(list(home.iterdir()),[])

    def test_environment_style_and_headless_ipc_boundaries(self):
        env = {'DISPLAY': ':1', 'XAUTHORITY': '/auth', 'WAYLAND_DISPLAY': 'wayland-private',
               'QT_QPA_PLATFORM': 'wayland', 'QML_IMPORT_PATH': '/linux', 'QSG_RENDER_LOOP': 'basic',
               'LD_PRELOAD': '/injected', 'LD_LIBRARY_PATH': '/foreign', 'DPREFIX': '/foreign',
               'OPENKARYON_MACOS_QT_STYLE': 'native', 'KARYO_MACOS_QT_STYLE': 'native'}
        plan = self.plan(env)
        for name in ('QT_QPA_PLATFORM','QML_IMPORT_PATH','QSG_RENDER_LOOP','LD_PRELOAD','LD_LIBRARY_PATH',
                     'KARYO_MACOS_QT_STYLE','OPENKARYON_MACOS_QT_STYLE'):self.assertNotIn(name,plan.environment)
        self.assertEqual(plan.environment['XAUTHORITY'],'/auth')
        self.assertEqual(plan.environment['DARLING_SHARE_X11_IPC'],'1')
        self.assertEqual(plan.qt_style,'native'); self.assertNotIn('QT_STYLE_OVERRIDE',plan.argv[6])
        headless = self.plan({'DARLING_SHARE_X11_IPC': '1'})
        self.assertNotIn('DARLING_SHARE_X11_IPC',headless.environment)
        self.assertIsNone(headless.compatibility_profile); self.assertEqual(headless.argv[:2],['/test/darling','exec'])

    def test_invalid_style_and_missing_exec_permission_fail_before_state(self):
        with self.assertRaises(LaunchError):self.plan({'KARYO_MACOS_QT_STYLE':'fusion; touch /tmp/bad'})
        self.binary.chmod(0o600)
        with self.assertRaisesRegex(LaunchError,'права исполнения'):self.plan()
        self.assertFalse((self.home/'.karyo-macos').exists())

    def test_real_bash_preserves_positional_arguments_and_lower_file(self):
        plan = self.plan(); version = self.folder/'SystemVersion.plist'; lower = self.folder/'lower'
        lower.write_bytes(b'untouched lower'); os.link(lower,version)
        executable = self.folder/"probe $(literal)"; report = self.folder/'args.json'
        executable.write_text('#!'+sys.executable+'\nimport json,os,sys\nfrom pathlib import Path\n'
            'Path(sys.argv[1]).write_text(json.dumps({"args":sys.argv[2:],"style":os.getenv("QT_STYLE_OVERRIDE")}))\n')
        executable.chmod(0o755)
        args = ["a b '$()'", '; touch '+str(self.folder/'must-not-exist'), 'one\ntwo']
        script = profiles.GRAPHICAL_LAUNCH_SCRIPT.replace('/System/Library/CoreServices/SystemVersion.plist',str(version))
        subprocess.run(['/bin/bash','--noprofile','--norc','-c',script,'karyo-macos',str(plan.compatibility_profile),
            str(executable),str(report),*args],check=True)
        self.assertEqual(json.loads(report.read_text()),{'args':args,'style':'Fusion'})
        self.assertEqual(version.read_bytes(),profiles.GRAPHICAL_SYSTEM_VERSION)
        self.assertEqual(lower.read_bytes(),b'untouched lower')
        self.assertFalse((self.folder/'must-not-exist').exists())
        self.assertEqual(list(self.folder.glob('SystemVersion.plist.karyo-*')),[])

    def test_metadata_log_is_exclusive_and_never_claims_app_pass(self):
        plan = self.plan()
        with profiles.open_log(plan) as stream:stream.write(b'wrapper fixture only\n')
        header = json.loads(plan.log.read_text().splitlines()[0])
        self.assertEqual(header['ownerUid'],self.uid)
        self.assertNotIn('passed',header)
        self.assertEqual(stat.S_IMODE(plan.log.stat().st_mode),0o600)
        with self.assertRaises(FileExistsError):profiles.open_log(plan)

    def test_overlay_requires_owned_private_tmpfs_runtime(self):
        runtime = self.folder/'runtime'; runtime.mkdir(mode=0o700)
        def filesystem(path):return 'tmpfs' if Path(path)==runtime else 'overlay'
        with patch.object(profiles,'mount_filesystem_type',side_effect=filesystem):
            plan = self.plan({'XDG_RUNTIME_DIR':str(runtime)})
            self.assertTrue(plan.prefix.is_relative_to(runtime/'karyo-macos'))
            self.assertFalse(plan.persistent)
            runtime.chmod(0o755)
            with self.assertRaises(LaunchError):self.plan({'XDG_RUNTIME_DIR':str(runtime)})
            for value in ('','relative',str(self.folder/'missing')):
                with self.assertRaises(LaunchError):self.plan({'XDG_RUNTIME_DIR':value})
        runtime.chmod(0o700)
        with patch.object(profiles,'mount_filesystem_type',return_value='overlay'):
            with self.assertRaises(LaunchError):self.plan({'XDG_RUNTIME_DIR':str(runtime)})

    def test_mountinfo_longest_path_and_escaped_space(self):
        source = self.folder/'mountinfo'
        source.write_text('1 0 0:1 / / rw - ext4 dev rw\n2 1 0:2 / /home/a\\040b rw - tmpfs tmpfs rw\n')
        self.mount.stop()
        self.assertEqual(profiles.mount_filesystem_type('/home/a b/child',source),'tmpfs')
        self.assertEqual(profiles.mount_filesystem_type('/home/a bb/child',source),'ext4')

    def script_case(self, style='fusion', missing=False, preexisting=False):
        plan=self.plan({'DISPLAY': ':1','KARYO_MACOS_QT_STYLE':style})
        version=self.folder/'version';version.write_bytes(b'original metadata')
        marker=self.folder/'ran';executable=self.folder/'engine-probe'
        executable.write_text('#!'+sys.executable+'\nimport json,os,sys\nfrom pathlib import Path\n'
            'Path(sys.argv[1]).write_text(json.dumps([os.getenv("QT_STYLE_OVERRIDE"),os.getenv("QT_MAC_WANTS_LAYER")]))\n')
        executable.chmod(0o755)
        script=profiles.GRAPHICAL_LAUNCH_SCRIPTS[style].replace('/System/Library/CoreServices/SystemVersion.plist',str(version))
        if preexisting:
            script='ln -s "$1" "${1}.karyo-$$"\nshift\n'+script
        source=str(self.folder/'missing') if missing else str(plan.compatibility_profile)
        result=subprocess.run(['/bin/bash','--noprofile','--norc','-c',script,'karyo-macos',
            *([str(version)] if preexisting else []),source,str(executable),str(marker)],capture_output=True,text=True)
        return result,version,marker

    def test_native_style_does_not_force_fusion(self):
        result,version,marker=self.script_case(style='native')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(marker.read_text()),[None,'0'])
        self.assertEqual(version.read_bytes(),profiles.GRAPHICAL_SYSTEM_VERSION)

    def test_missing_source_keeps_lower_metadata_and_never_executes(self):
        result,version,marker=self.script_case(missing=True)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(marker.exists());self.assertEqual(version.read_bytes(),b'original metadata')
        self.assertEqual(list(self.folder.glob('version.karyo-*')),[])

    def test_existing_temporary_alias_is_not_overwritten_or_deleted(self):
        result,version,marker=self.script_case(preexisting=True)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(marker.exists());self.assertEqual(version.read_bytes(),b'original metadata')
        aliases=list(self.folder.glob('version.karyo-*'))
        self.assertEqual(len(aliases),1);self.assertTrue(aliases[0].is_symlink())
