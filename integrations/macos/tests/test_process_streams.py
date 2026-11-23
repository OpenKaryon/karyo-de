# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary Linux stream fixtures only; no Darling compatibility claim."""
import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_macos import process


class OutputStreams(unittest.TestCase):
    def invoke(self, body):
        with tempfile.TemporaryDirectory(prefix='karyo-streams-') as directory:
            folder=Path(directory)
            script=folder/'fixture.py'
            script.write_text(body)
            plan=SimpleNamespace(argv=[sys.executable,str(script)],environment=os.environ.copy(),
                                 cwd=folder,log=folder/'fixture.log')
            output,error=io.BytesIO(),io.BytesIO()
            with patch.object(process,'open_log',lambda _plan:plan.log.open('wb')), \
                 contextlib.redirect_stderr(io.StringIO()):
                code=process.wait_launch(plan,output,error)
            return code,output.getvalue(),error.getvalue(),plan.log.read_bytes()

    def test_distinct_streams_and_exit_status(self):
        code,out,err,log=self.invoke('import os,sys\nos.write(1,b"Darwin\\n")\n'
                                   'os.write(2,b"renderer diagnostic\\n")\nsys.exit(17)\n')
        self.assertEqual((code,out,err),(17,b'Darwin\n',b'renderer diagnostic\n'))
        self.assertIn(out,log);self.assertIn(err,log)
        self.assertIn(b'Karyo exit code: 17',log)

    def test_stdout_eof_does_not_discard_later_stderr(self):
        code,out,err,_log=self.invoke('import os,time\nos.write(1,b"result\\n")\n'
                                    'os.close(1)\ntime.sleep(.2)\nos.write(2,b"late diagnostic\\n")\n')
        self.assertEqual((code,out,err),(0,b'result\n',b'late diagnostic\n'))

    def test_both_pipes_are_drained_without_blocking_each_other(self):
        body='import os\nfor _ in range(64):\n os.write(2,b"e"*4096)\n os.write(1,b"o"*4096)\n'
        code,out,err,log=self.invoke(body)
        self.assertEqual((code,out,err),(0,b'o'*262144,b'e'*262144))
        payload=log.removesuffix(b'\nKaryo exit code: 0\n')
        self.assertEqual(payload.count(b'o'),len(out))
        self.assertEqual(payload.count(b'e'),len(err))
        self.assertEqual(len(payload),len(out)+len(err))

    def test_quiet_live_child_is_not_timed_out(self):
        started=time.monotonic()
        code,out,err,_log=self.invoke('import time\ntime.sleep(.65)\nprint("finished")\n')
        self.assertGreaterEqual(time.monotonic()-started,.6)
        self.assertEqual((code,out,err),(0,b'finished\n',b''))

    def test_daemon_held_pipes_do_not_delay_finished_child(self):
        with tempfile.TemporaryDirectory(prefix='karyo-stream-daemon-') as directory:
            folder=Path(directory);stop=folder/'stop';pidfile=folder/'pid'
            script=folder/'fixture.py'
            script.write_text('import os,sys,time\nfrom pathlib import Path\n'
                'stop,pidfile=map(Path,sys.argv[1:])\npid=os.fork()\n'
                'if pid==0:\n while not stop.exists():time.sleep(.02)\n os._exit(0)\n'
                'pidfile.write_text(str(pid))\nos.write(1,b"finished\\n")\nos.write(2,b"diagnostic\\n")\n')
            out,err,log=io.BytesIO(),io.BytesIO(),io.BytesIO()
            child=subprocess.Popen([sys.executable,str(script),str(stop),str(pidfile)],
                                   stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            started=time.monotonic()
            try:
                process.stream_output(child,log,out,err)
                self.assertEqual(child.wait(timeout=2),0)
                self.assertLess(time.monotonic()-started,2)
                self.assertEqual((out.getvalue(),err.getvalue()),(b'finished\n',b'diagnostic\n'))
                os.kill(int(pidfile.read_text()),0)
            finally:
                stop.touch();child.stdout.close();child.stderr.close();child.wait(timeout=2)


if __name__=='__main__':
    unittest.main()
