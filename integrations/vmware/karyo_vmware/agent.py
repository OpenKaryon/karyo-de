# SPDX-License-Identifier: GPL-3.0-or-later
"""One foreground vmusr per unlocked active native desktop; no privilege gain."""
import fcntl,os,signal,stat,subprocess,sys,time
from pathlib import Path
from .children import Child
from .display import enabled
from .session import Session
from .worker import LOCK,drop_privileges,validate_lock

DELAYS=(2,5,10,30,60)

def on_vmware():
    try:return subprocess.run(['/usr/bin/systemd-detect-virt','--vm'],capture_output=True,text=True,timeout=3).stdout.strip()=='vmware'
    except (OSError,subprocess.SubprocessError):return False

def acquire():
    parent=Path(LOCK).parent.lstat()
    if not stat.S_ISDIR(parent.st_mode) or parent.st_uid!=0 or stat.S_IMODE(parent.st_mode)!=0o755:raise PermissionError('Unsafe VMware runtime')
    fd=os.open(LOCK,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        validate_lock(fd);fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB);return fd
    except BlockingIOError:os.close(fd);return None
    except BaseException:os.close(fd);raise

def external_vmusr():
    for path in Path('/proc').iterdir():
        if not path.name.isdecimal():continue
        try:
            with (path/'cmdline').open('rb') as stream:args=stream.read(65536).split(bytes([0]))
            if args and args[0].rsplit(b'/',1)[-1]==b'vmtoolsd' and (b'vmusr' in args or b'--name=vmusr' in args):return True
        except (FileNotFoundError,ProcessLookupError,PermissionError):pass
    return False

class Agent:
    def __init__(self,commands,*,clock=time.monotonic,binder=acquire,factory=Child,external=external_vmusr,display=None):
        self.display=display;self.commands=commands;self.clock=clock;self.binder=binder;self.factory=factory;self.external=external
        self.lock=None;self.children=[];self.active=False;self.due=0;self.started=0;self.failures=0

    def stop(self):
        for child in self.children:child.stop()
        self.children=[]
        if self.lock is not None:os.close(self.lock);self.lock=None

    def tick(self,active):
        now=self.clock()
        if not active:
            self.stop();self.active=False;self.due=now+1.5;self.failures=0;return
        self.active=True
        if self.children:
            if all(child.alive() for child in self.children):
                if now-self.started>60:self.failures=0
                return
            self.stop();self.due=now+DELAYS[min(self.failures,len(DELAYS)-1)];self.failures+=1
        if now<self.due:return
        self.lock=self.binder()
        if self.lock is None:self.due=now+.5;return
        try:
            if self.external():self.stop();self.due=now+.5;return
            for command in self.commands:self.children.append(self.factory(command,self.lock))
            if self.display is not None:self.children.append(self.display(self.lock))
            self.started=now
        except BaseException:self.stop();raise

def main():
    if os.getuid()!=os.geteuid() or os.getgid()!=os.getegid() or not 1000<=os.getuid()<65534:return 2
    if sys.argv[1:]:return 2
    if not on_vmware():return 0
    if not os.environ.get('DISPLAY') or not os.environ.get('WAYLAND_DISPLAY'):
        print('karyo-vmware-agent: native X11 and Wayland displays are required.',file=sys.stderr);return 1
    drop_privileges();session=None;agent=None
    def stop(*unused):raise SystemExit(0)
    for number in (signal.SIGTERM,signal.SIGHUP,signal.SIGINT):signal.signal(number,stop)
    try:
        session=Session()
        def display(lock):return Child(['/usr/libexec/karyo/vmware/display-follower',str(lock)],lock)
        agent=Agent([['/usr/bin/vmtoolsd','-n','vmusr'],['/usr/bin/karyo-xclipboard']],display=display if enabled(os.environ) else None)
        while True:
            active=session.state()
            if active is None:break
            agent.tick(active);time.sleep(.25)
        return 0
    except (OSError,ValueError,RuntimeError):
        print('karyo-vmware-agent: native session or channel runtime is unavailable.',file=sys.stderr);return 1
    finally:
        if agent is not None:agent.stop()
        if session is not None:session.close()
