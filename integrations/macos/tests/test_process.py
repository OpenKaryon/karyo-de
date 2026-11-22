# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Linux subprocess fixtures; never a Darling compatibility pass."""
import contextlib
from dataclasses import replace
import io
import json
import os
from pathlib import Path
import sys
import threading
import time
import unittest
from support import ProfileFixture
from karyo_macos import process
from karyo_macos.errors import LaunchError


class Process(ProfileFixture, unittest.TestCase):
    def stub(self, body, arguments=()):
        engine = self.folder/'engine'
        engine.write_text('#!'+sys.executable+'\n'+body)
        engine.chmod(0o755)
        plan = self.plan(environment={},arguments=list(arguments))
        return replace(plan,argv=[str(engine),*plan.argv[1:]])

    def run_plan(self,plan):
        output=io.BytesIO()
        with contextlib.redirect_stderr(io.StringIO()):code=process.wait_launch(plan,output)
        return code,output.getvalue()

    def test_literal_argv_diagnostics_and_status(self):
        args=["spaces '$()'",';touch /tmp/must-not-exist','one\ntwo','--flag']
        plan=self.stub('import json,sys\nprint(json.dumps(sys.argv[1:]))\n'
                       'print("diagnostic",file=sys.stderr)\nsys.exit(23)\n',args)
        code,output=self.run_plan(plan)
        self.assertEqual(code,23)
        line=next(line for line in output.decode().splitlines() if line.startswith('['))
        self.assertEqual(json.loads(line),['exec',str(self.binary),*args])
        self.assertIn(b'diagnostic',plan.log.read_bytes())
        self.assertIn(b'Karyo exit code: 23',plan.log.read_bytes())

    def test_live_quiet_child_has_no_application_timeout(self):
        plan=self.stub('import os,time\nos.write(1,b"before\\n")\ntime.sleep(.65)\nos.write(1,b"after\\n")\n')
        begin=time.monotonic();code,output=self.run_plan(plan)
        self.assertGreaterEqual(time.monotonic()-begin,.6)
        self.assertEqual((code,output),(0,b'before\nafter\n'))

    def test_persistent_daemon_pipe_does_not_block_finished_child(self):
        stop=self.folder/'stop';pidfile=self.folder/'pid'
        plan=self.stub('import os,sys,time\nfrom pathlib import Path\n'
            'stop,pidfile=map(Path,sys.argv[-2:])\npid=os.fork()\n'
            'if pid==0:\n while not stop.exists():time.sleep(.02)\n os._exit(0)\n'
            'pidfile.write_text(str(pid))\nos.write(1,b"finished\\n")\nsys.exit(23)\n',[str(stop),str(pidfile)])
        result={}
        def worker():
            try:result['value']=self.run_plan(plan)
            except BaseException as error:result['error']=error
        thread=threading.Thread(target=worker,daemon=True);thread.start()
        try:
            thread.join(3)
            self.assertFalse(thread.is_alive(),'Waiting for daemon-held EOF is a regression')
            self.assertNotIn('error',result)
            self.assertEqual(result['value'],(23,b'finished\n'))
            pid=int(pidfile.read_text());os.kill(pid,0)
            self.assertNotEqual(Path('/proc/'+str(pid)+'/stat').read_text().rsplit(')',1)[1].split()[0],'Z')
        finally:
            stop.touch();thread.join(3)

    def test_exec_error_has_a_private_log(self):
        plan=self.plan(environment={});plan=replace(plan,argv=[str(self.folder/'missing')])
        with contextlib.redirect_stderr(io.StringIO()),self.assertRaisesRegex(LaunchError,'Не удалось запустить Darling'):
            process.wait_launch(plan,io.BytesIO())
        self.assertIn('Darling launch error:',plan.log.read_text())

    def test_signal_exit_maps_to_shell_status(self):
        plan=self.stub('import os,signal\nos.kill(os.getpid(),signal.SIGTERM)\n')
        self.assertEqual(self.run_plan(plan)[0],143)
