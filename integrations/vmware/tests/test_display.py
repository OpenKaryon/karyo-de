# SPDX-License-Identifier: GPL-3.0-or-later
import os,subprocess,sys,tempfile,unittest
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from karyo_vmware.display import Follower,current_modes,enabled,preferred_modes


def output(mode='1280x800',*,active=True,name='Virtual-1'):
    return name+' "VMware display"\n  Enabled: '+('yes' if active else 'no')+'\n  Modes:\n    '+mode+' px, 60.000000 Hz (preferred, current)\n'


class KernelModesTests(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory();self.addCleanup(self.directory.cleanup);self.root=Path(self.directory.name)

    def connector(self,card,name,mode,*,driver='vmwgfx',connected=True):
        target=self.root/'drivers'/driver;target.mkdir(parents=True,exist_ok=True)
        device=self.root/card/'device';device.mkdir(parents=True,exist_ok=True)
        if not (device/'driver').exists():(device/'driver').symlink_to(target)
        value=self.root/(card+'-'+name);value.mkdir()
        (value/'status').write_text('connected' if connected else 'disconnected')
        (value/'modes').write_text(mode+'\n800x600\n');return value

    def test_only_connected_vmware_connectors_are_followed(self):
        self.connector('card0','Virtual-1','1152x720')
        self.connector('card0','Virtual-2','1024x768',connected=False)
        self.connector('card1','DP-1','1920x1080',driver='i915')
        self.assertEqual(preferred_modes(self.root),{'Virtual-1':'1152x720'})

    def test_invalid_or_ambiguous_output_is_not_addressed(self):
        value=self.connector('card0','Virtual-1','99999x720')
        self.assertEqual(preferred_modes(self.root),{})
        (value/'modes').write_text('1152x720\n');self.connector('card1','Virtual-1','1280x800')
        self.assertEqual(preferred_modes(self.root),{})
        (value/'modes').write_text('--invalid\n');self.assertEqual(preferred_modes(self.root),{'Virtual-1':'1280x800'})

    def test_disappearing_connector_is_ignored(self):
        value=self.connector('card0','Virtual-1','1152x720');(value/'modes').unlink()
        self.assertEqual(preferred_modes(self.root),{})


class FollowerTests(unittest.TestCase):
    def setUp(self):
        self.now=10.;self.preferred={'Virtual-1':'1152x720'};self.requests=[];self.active=True;self.current='1280x800';self.fail=False
        def request(arguments):
            self.requests.append(arguments)
            if self.fail:raise subprocess.TimeoutExpired('wlr-randr',2)
            if arguments:self.current=arguments[-1];return ''
            return output(self.current)
        self.follower=Follower(request,lambda:self.active,clock=lambda:self.now,read=lambda:dict(self.preferred))

    def tick(self,after=0,active=True):self.now+=after;self.follower.tick(active)

    def test_host_changes_debounce_and_unchanged_map_does_not_fork(self):
        self.tick();self.assertFalse(self.requests)
        self.preferred['Virtual-1']='1024x768';self.tick(.5);self.assertFalse(self.requests)
        self.tick(.5);self.assertEqual(self.current,'1024x768')
        self.assertEqual(self.requests,[[],['--output','Virtual-1','--custom-mode','1024x768']])
        self.tick(3);self.assertEqual(len(self.requests),3)
        self.current='800x600';self.tick(10);self.assertEqual(len(self.requests),3)
        self.preferred['Virtual-1']='1280x800';self.tick(.5);self.tick(.5);self.assertEqual(self.current,'1280x800')

    def test_background_cancels_request_and_activation_reads_fresh_mode(self):
        self.tick();self.tick(.5,active=False);self.assertFalse(self.requests)
        self.preferred['Virtual-1']='1024x768';self.tick(.5);self.tick(.5);self.assertEqual(self.current,'1024x768')

    def test_lock_or_unknown_native_state_cannot_apply_a_pending_mode(self):
        for state in (False,None):
            self.active=state;self.tick();self.tick(.5);self.assertFalse(self.requests)
        self.active=True;self.tick(.5);self.assertEqual(self.current,'1152x720')

    def test_state_is_checked_again_between_query_and_mode_change(self):
        def request(arguments):self.requests.append(arguments);self.active=False;return output()
        self.follower.request=request;self.tick();self.tick(.5);self.assertEqual(self.requests,[[]])

    def test_timeout_backs_off_without_disabling_future_following(self):
        self.fail=True;self.tick();self.tick(.5);self.assertEqual(len(self.requests),1)
        self.tick(4.5);self.assertEqual(len(self.requests),1)
        self.fail=False;self.tick(.5);self.assertEqual(self.current,'1152x720')

    def test_disabled_or_unknown_outputs_are_not_enabled_by_a_resize(self):
        def request(arguments):self.requests.append(arguments);return output(active=False)+output(name='Virtual-2')
        self.follower.request=request;self.tick();self.tick(.5);self.assertEqual(self.requests,[[]])

    def test_recheck_corrects_one_late_competing_mode_change(self):
        self.tick();self.tick(.5);self.current='800x600';self.tick(3)
        self.assertEqual(self.current,'1152x720');self.current='800x600';self.tick(3);self.assertEqual(self.current,'800x600')


class DisplayContractTests(unittest.TestCase):
    def test_only_explicit_current_enabled_modes_are_parsed(self):
        self.assertEqual(current_modes(output()),{'Virtual-1':'1280x800'})
        self.assertEqual(current_modes(output(active=False)),{})
        self.assertEqual(current_modes('Virtual-1\n  Enabled: yes\n    800x600 px, 60.0 Hz (preferred)\n'),{})

    def test_debian_custom_mode_can_omit_refresh_rate(self):
        text='Virtual-1 \"VMware\"\n  Enabled: yes\n    1280x800 px, 60.0 Hz (preferred)\n    1152x720 px (current)\n'
        self.assertEqual(current_modes(text),{'Virtual-1':'1152x720'})

    def test_current_opt_out_overrides_legacy_default(self):
        self.assertTrue(enabled({}));self.assertFalse(enabled({'OPENKARYON_VMWARE_DISPLAY_SYNC':'0'}))
        self.assertFalse(enabled({'KARYO_VMWARE_DISPLAY_SYNC':'0'}))
        self.assertTrue(enabled({'KARYO_VMWARE_DISPLAY_SYNC':'1','OPENKARYON_VMWARE_DISPLAY_SYNC':'0'}))


if __name__=='__main__':unittest.main()
