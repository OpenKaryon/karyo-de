# SPDX-License-Identifier: GPL-3.0-or-later
"""Real tiny native DEBs prove multiarch identity/content failures before install."""
import importlib.util,os,shutil,subprocess,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('wine_shared',Path(__file__).with_name('check-wine-shared.py'))
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)


@unittest.skipUnless(shutil.which('dpkg-deb'),'Native Debian package tooling required')
class CoinstallationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.count=0

    def package(self,architecture,version='1.0-1',text='same',mode=0o644):
        self.count+=1;folder=self.root/str(self.count);control=folder/'DEBIAN';control.mkdir(parents=True)
        (control/'control').write_text('Package: libwine\nVersion: '+version+'\nArchitecture: '+architecture+'\nMulti-Arch: same\nMaintainer: QA <qa@openkaryon.invalid>\nDescription: Synthetic shared-payload fixture\n')
        common=folder/'usr/share/wine/fixture';common.parent.mkdir(parents=True)
        common.write_text(text);common.chmod(mode)
        native=folder/('usr/lib/'+architecture+'-qa/fixture');native.parent.mkdir(parents=True);native.write_bytes(b'QA native payload')
        archive=self.root/(str(self.count)+'.deb')
        subprocess.run(['dpkg-deb','--root-owner-group','--build',str(folder),str(archive)],check=True,stdout=subprocess.DEVNULL)
        return archive

    def test_matching_shared_bytes_and_metadata_pass(self):
        result=shared.check(self.package('amd64'),self.package('i386'))
        self.assertTrue(result['accepted']);self.assertEqual(result['shared_files'],1)

    def test_wrong_version_and_architecture_fail_before_payload(self):
        amd64=self.package('amd64')
        with self.assertRaisesRegex(ValueError,'versions differ'):shared.check(amd64,self.package('i386',version='2.0-1'))
        with self.assertRaisesRegex(ValueError,'package identities'):shared.check(amd64,self.package('amd64'))

    def test_shared_content_and_mode_mismatch_are_rejected(self):
        amd64=self.package('amd64')
        with self.assertRaisesRegex(ValueError,'payload differs'):shared.check(amd64,self.package('i386',text='changed'))
        with self.assertRaisesRegex(ValueError,'payload differs'):shared.check(amd64,self.package('i386',mode=0o600))


if __name__=='__main__':unittest.main()
