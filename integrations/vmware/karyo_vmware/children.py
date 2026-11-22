# SPDX-License-Identifier: GPL-3.0-or-later
"""Foreground groups retain an unreaped leader until their captured group dies."""
import os,signal,subprocess,sys,time
from pathlib import Path

class Child:
    def __init__(self,command,lock):
        self.process=subprocess.Popen([sys.executable,'-I',str(Path(__file__).with_name('worker.py')),
            str(os.getpid()),str(lock),*command],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,start_new_session=True,pass_fds=(lock,))

    def alive(self):
        # Unlike Popen.poll(), WNOWAIT pins the dead leader's PID/group identity.
        return os.waitid(os.P_PID,self.process.pid,os.WEXITED|os.WNOHANG|os.WNOWAIT) is None

    def live_group(self):
        for path in Path('/proc').iterdir():
            if not path.name.isdecimal():continue
            try:
                fields=(path/'stat').read_text().rpartition(')')[2].split()
                if fields[0]!='Z' and int(fields[2])==self.process.pid:return True
            except (FileNotFoundError,ProcessLookupError,PermissionError):pass
        return False

    def stop(self):
        if self.process.returncode is not None:return
        try:os.killpg(self.process.pid,signal.SIGTERM)
        except ProcessLookupError:pass
        deadline=time.monotonic()+2
        while self.live_group() and time.monotonic()<deadline:time.sleep(.03)
        if self.live_group():
            try:os.killpg(self.process.pid,signal.SIGKILL)
            except ProcessLookupError:pass
            deadline=time.monotonic()+2
            while self.live_group() and time.monotonic()<deadline:time.sleep(.03)
        if self.live_group():raise RuntimeError('Owned VMware group did not stop; channel remains reserved')
        self.process.wait(timeout=2)
