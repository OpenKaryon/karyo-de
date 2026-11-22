# SPDX-License-Identifier: GPL-3.0-or-later
import json,os,stat,struct,subprocess,sys,tempfile,types,unittest
from pathlib import Path
from unittest.mock import patch
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_win32 import launcher as backend,policy,storage,desktop


def pe_file(path):
    payload=bytearray(68);payload[:2]=b'MZ';struct.pack_into('<I',payload,60,64);payload[64:]=b'PE\0\0';path.write_bytes(payload);return path


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.home=Path(self.temp.name)
        self.file=pe_file(self.home/'Пример $(touch nope).EXE')
        self.environment={'HOME':str(self.home),'DISPLAY':':3','WAYLAND_DISPLAY':'wayland-1','DBUS_SESSION_BUS_ADDRESS':'unix:path=/private/bus'}
        self.enterContext(patch.object(backend,'executable',side_effect=lambda name:'/usr/bin/'+name))

    def context(self,identifier=None,environment=None):
        return backend.launch_context(self.file,'exe',self.home,environment or self.environment,identifier)

    def test_arguments_are_not_interpolated_into_shell_or_cwd(self):
        context=self.context();child=types.SimpleNamespace(pid=123,wait=lambda:0)
        with patch.object(backend.subprocess,'Popen',return_value=child) as start:
            backend.launch(context,['a b','--literal','$(echo nope)'],self.environment,self.home,register=False)
        self.assertEqual(start.call_args.args[0],['/usr/bin/wine',str(self.file),'a b','--literal','$(echo nope)'])
        self.assertNotIn('shell',start.call_args.kwargs);self.assertEqual(start.call_args.kwargs['cwd'],str(self.home))
        for p in (self.home/'.local/share/karyo/windows/logs').iterdir():self.assertEqual(stat.S_IMODE(p.stat().st_mode),0o600)

    def test_actual_home_selects_profile_and_old_slot_cannot_select_another(self):
        first=self.context(environment={'OPENKARYON_SESSION_SLOT':'1'});second=self.context(environment={'OPENKARYON_SESSION_SLOT':'2','HOME':'/other'})
        self.assertEqual(first,second)
        other=backend.launch_context(self.file,'exe',self.home/'work',self.environment)
        self.assertNotEqual(first['prefix'],other['prefix']);self.assertEqual(first['ownerUid'],os.getuid())
        self.assertFalse((self.home/'.local').exists())

    def test_explicit_application_profile_shares_installer_and_executable(self):
        installer=self.home/'Setup Пример.msi';installer.write_bytes(b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1'+b'\0'*56)
        path,kind=backend.validate_application(installer)
        one=backend.launch_context(path,kind,self.home,self.environment,'example-suite');two=self.context('example-suite')
        self.assertEqual(one['prefix'],two['prefix']);self.assertNotEqual(one['prefix'],self.context()['prefix'])

    def test_installed_executable_keeps_its_existing_profile(self):
        folder=self.home/'.local/share/karyo/windows/prefixes/installer-id/drive_c/Program Files/Test';folder.mkdir(parents=True)
        installed=pe_file(folder/'test.exe');context=backend.launch_context(installed,'exe',self.home,{})
        self.assertEqual(context['applicationId'],'installer-id');self.assertEqual(context['prefix'],str(folder.parents[2]))
        with self.assertRaises(policy.LaunchError):backend.launch_context(installed,'exe',self.home,{},'different-id')

    def test_invalid_identifiers_and_native_files_are_rejected(self):
        for identifier in ('../escape','','-starts','a'*81,'has space'):
            with self.subTest(identifier=identifier),self.assertRaises(policy.LaunchError):self.context(identifier)
        for name,data in [('fake.exe',b'MZbad'),('fake.msi',b'MZ'+b'\0'*62),('native',b'\x7fELF')]:
            p=self.home/name;p.write_bytes(data)
            with self.assertRaises(policy.LaunchError):backend.validate_application(p)

    def test_wine_environment_preserves_private_bus_and_scrubs_linux_qt(self):
        env={**self.environment,'QT_QPA_PLATFORM':'wayland','QT_QPA_PLATFORMTHEME':'karyo','QT_PLUGIN_PATH':'/linux/plugins','QT_QUICK_BACKEND':'software','QT_STYLE_OVERRIDE':'karyo','QML2_IMPORT_PATH':'/linux/qml','QSG_RHI_BACKEND':'vulkan','WINEPREFIX':'/unrelated','WINEARCH':'win32','WINEDLLOVERRIDES':'d3d11=n,b','LD_PRELOAD':'/override.so'}
        result=backend.wine_environment(self.context(),env)
        for key in ('DISPLAY','WAYLAND_DISPLAY','DBUS_SESSION_BUS_ADDRESS'):self.assertEqual(result[key],env[key])
        self.assertEqual(result['WINEPREFIX'],self.context()['prefix']);self.assertEqual(result['WINEARCH'],'win64')
        self.assertFalse(any(k.startswith(('QT_','QML','QSG_')) for k in result));self.assertNotIn('LD_PRELOAD',result)
        self.assertEqual(result['WINEDLLOVERRIDES'],'d3d11=n,b;winemenubuilder.exe=d');self.assertEqual(env['WINEPREFIX'],'/unrelated')

    def test_existing_prefix_does_not_force_a_new_wine_architecture(self):
        context=self.context('legacy-suite')
        prefix=Path(context['prefix']);prefix.mkdir(parents=True)
        registry=prefix/'system.reg'
        for header in (b'WINE REGISTRY Version 2\n#arch=win32\n',
                       b'WINE REGISTRY Version 2\n#arch=win64\n',
                       b'WINE REGISTRY Version 2\n'):
            with self.subTest(header=header):
                registry.write_bytes(header)
                env=backend.wine_environment(context,{**self.environment,'WINEARCH':'win64'})
                self.assertNotIn('WINEARCH',env)
                self.assertEqual(env['WINEPREFIX'],str(prefix))
                self.assertEqual(registry.read_bytes(),header)
        registry.unlink()
        self.assertEqual(backend.wine_environment(context,self.environment)['WINEARCH'],'win64')

    def test_msi_conversion_is_bounded_and_installer_arguments_remain_literal(self):
        context=self.context();context['kind']='msi';child=types.SimpleNamespace(pid=124,wait=lambda:0)
        with patch.object(backend.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'Z:\\Setup Пример.msi\n')) as convert,patch.object(backend.subprocess,'Popen',return_value=child) as start:
            backend.launch(context,['/qn'],self.environment,self.home)
        self.assertEqual(convert.call_args.args[0],['/usr/bin/winepath','--windows',str(self.file)])
        self.assertEqual(convert.call_args.kwargs['timeout'],120)
        self.assertEqual(start.call_args.args[0],['/usr/bin/wine','msiexec','/i','Z:\\Setup Пример.msi','/qn'])
        self.assertFalse((self.home/'.local/share/applications').exists())

    def test_failed_or_hung_converter_never_starts_installer(self):
        context=self.context();context['kind']='msi'
        for result in (subprocess.CompletedProcess([],3,''),subprocess.TimeoutExpired('winepath',120)):
            with patch.object(backend.subprocess,'run',**({'side_effect':result} if isinstance(result,Exception) else {'return_value':result})),patch.object(backend.subprocess,'Popen') as start:
                with self.assertRaises(policy.LaunchError):backend.launch(context,[],self.environment,self.home)
                start.assert_not_called()

    def test_process_failure_records_the_actual_code_without_arguments(self):
        child=types.SimpleNamespace(pid=125,wait=lambda:42)
        with patch.object(backend.subprocess,'Popen',return_value=child):
            with self.assertRaisesRegex(policy.LaunchError,'42'):backend.launch(self.context(),['private-argument'],self.environment,self.home,register=False)
        record=json.loads(next((self.home/'.local/share/karyo/windows/logs').glob('*.json')).read_text())
        self.assertEqual(record['returnCode'],42);self.assertEqual(record['processId'],125);self.assertIn('finishedAt',record)
        self.assertNotIn('private-argument',json.dumps(record))

    def test_registration_uses_identifier_and_owner_instead_of_exec_path(self):
        context=self.context();env={**self.environment,'XDG_DATA_HOME':str(self.home/'private session data')}
        with patch.object(desktop,'extract_icon',return_value=False):desktop.register_application(context,env,self.home)
        entry=next((Path(env['XDG_DATA_HOME'])/'applications').glob('*.desktop'));text=entry.read_text()
        self.assertIn('Exec=karyo-win32 --gui --registered '+context['applicationId']+'\n',text);self.assertNotIn(str(self.file),text)
        self.assertEqual(storage.registered_application(context['applicationId'],self.home),str(self.file))
        path=self.home/'.local/share/karyo/windows/applications'/(context['applicationId']+'.json');record=json.loads(path.read_text());record['ownerUid']+=1;storage.write_json(path,record)
        with self.assertRaises(policy.LaunchError):storage.registered_application(context['applicationId'],self.home)

    def test_symlink_record_and_storage_parent_are_rejected(self):
        outside=self.home/'outside';outside.mkdir();(self.home/'.local').symlink_to(outside)
        with self.assertRaises(OSError):storage.secure_directory(self.home/'.local/share/karyo/windows')
        self.assertFalse((outside/'share').exists());(self.home/'.local').unlink()
        folder=self.home/'.local/share/karyo/windows/applications';storage.secure_directory(folder)
        (folder/'example.json').symlink_to(self.file)
        with self.assertRaises(policy.LaunchError):storage.registered_application('example',self.home)

    def test_duplicate_record_fields_and_wrong_permissions_are_rejected(self):
        folder=self.home/'.local/share/karyo/windows/applications';storage.secure_directory(folder);p=folder/'example.json'
        storage.atomic_bytes(p,b'{"version":1,"version":1}')
        with self.assertRaises(policy.LaunchError):storage.registered_application('example',self.home)
        storage.write_json(p,{'version':1,'ownerUid':os.getuid(),'applicationId':'example','file':str(self.file)});p.chmod(0o644)
        with self.assertRaises(policy.LaunchError):storage.registered_application('example',self.home)

    def test_registered_parent_symlink_and_oversized_metadata_are_rejected(self):
        folder=self.home/'.local/share/karyo/windows/applications';storage.secure_directory(folder)
        record={'version':1,'ownerUid':os.getuid(),'applicationId':'example','file':str(self.file)}
        storage.write_json(folder/'example.json',record)
        alternate=self.home/'records';folder.rename(alternate);folder.symlink_to(alternate)
        with self.assertRaises(policy.LaunchError):storage.registered_application('example',self.home)
        folder.unlink();alternate.rename(folder)
        storage.atomic_bytes(folder/'example.json',b' '*1048577)
        with self.assertRaises(policy.LaunchError):storage.registered_application('example',self.home)

    def test_root_is_rejected_before_any_dialog_or_execution(self):
        with patch.object(policy.os,'getuid',return_value=0),patch.object(backend,'launch') as start,patch.object(backend,'choose_application') as choose,patch.object(backend,'show_error'):
            self.assertEqual(backend.main(['--gui',str(self.file)]),1)
        start.assert_not_called();choose.assert_not_called()

    def test_inspection_does_not_create_data_or_execute_wine(self):
        with patch.object(backend,'user_home',return_value=self.home),patch.dict(os.environ,self.environment,clear=True),patch.object(backend,'launch') as start,patch('builtins.print'),patch.object(backend,'drop_privileges') as drop:
            self.assertEqual(backend.main(['--inspect',str(self.file)]),0)
        start.assert_not_called();drop.assert_not_called();self.assertFalse((self.home/'.local').exists())

    def test_real_capability_reset_sets_no_new_privileges(self):
        code="import sys;sys.path.insert(0,"+repr(str(Path(__file__).resolve().parents[1]))+");from karyo_win32.policy import drop_privileges;drop_privileges();from pathlib import Path;print(Path('/proc/self/status').read_text())"
        data=subprocess.check_output([sys.executable,'-I','-B','-c',code],text=True);fields=dict(line.split(':',1) for line in data.splitlines() if ':' in line)
        self.assertEqual(fields['NoNewPrivs'].strip(),'1')
        for key in ('CapEff','CapPrm','CapInh','CapAmb'):self.assertEqual(int(fields[key],16),0)


if __name__=='__main__':unittest.main()
