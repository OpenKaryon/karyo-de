# SPDX-License-Identifier: GPL-3.0-or-later
"""Follow VMware's kernel preferred sizes without a global udev subscription."""
import os,re,subprocess,sys,time
from pathlib import Path
from .session import Session
from .worker import drop_privileges,validate_lock

MODE=re.compile(r'([1-9][0-9]{0,4})x([1-9][0-9]{0,4})')
NAME=re.compile(r'[A-Za-z0-9_.-]{1,128}')


def enabled(environment):
    return environment.get('KARYO_VMWARE_DISPLAY_SYNC',environment.get('OPENKARYON_VMWARE_DISPLAY_SYNC','1'))!='0'


def preferred_modes(root=Path('/sys/class/drm')):
    result={}
    for connector in sorted(root.glob('card*-*')):
        try:
            card=connector.name.split('-',1)[0];name=connector.name.split('-',1)[1]
            if (root/card/'device/driver').resolve(strict=True).name!='vmwgfx':continue
            if not NAME.fullmatch(name) or (connector/'status').read_text().strip()!='connected':continue
            with (connector/'modes').open() as stream:mode=stream.readline(64).strip()
            match=MODE.fullmatch(mode)
            if not match or max(map(int,match.groups()))>32768:continue
            # Two cards with the same output name cannot be safely addressed.
            if name in result:return {}
            result[name]=mode
        except (OSError,RuntimeError):continue
    return result


def current_modes(text):
    result={};name=None;enabled_output=False
    for line in text.splitlines():
        if line and not line[0].isspace():
            candidate=line.split()[0];name=candidate if NAME.fullmatch(candidate) else None;enabled_output=False
        elif line.strip()=='Enabled: yes':enabled_output=True
        elif name and enabled_output:
            match=re.fullmatch(r'\s+([0-9]+x[0-9]+) px\b.*\bcurrent\b.*',line)
            if match:result[name]=match[1]
    return result


class Follower:
    def __init__(self,request,state,*,clock=time.monotonic,read=preferred_modes):
        self.request=request;self.state=state;self.clock=clock;self.read=read
        self.observed=None;self.next_scan=0;self.due=None;self.checks=0;self.reported=False

    def tick(self,active):
        now=self.clock()
        if not active:
            self.observed=None;self.next_scan=0;self.due=None;return
        if now<self.next_scan:return
        self.next_scan=now+.5
        preferred=self.read()
        if preferred!=self.observed:
            self.observed=preferred;self.due=now+.5 if preferred else None;self.checks=1;return
        if self.due is None or now<self.due:return
        self.due=None
        try:
            if self.state() is not True:self.due=now+.5;return
            current=current_modes(self.request([]))
            for name,mode in preferred.items():
                if name in current and current[name]!=mode:
                    if self.state() is not True:self.due=now+.5;return
                    self.request(['--output',name,'--custom-mode',mode])
            self.reported=False
            if self.checks:self.checks-=1;self.due=now+3
        except (OSError,subprocess.SubprocessError):
            self.due=now+5
            if not self.reported:print('karyo-vmware-display: mode request failed; retrying.',file=sys.stderr,flush=True)
            self.reported=True


def main():
    if (os.getuid()!=os.geteuid() or os.getgid()!=os.getegid() or not 1000<=os.getuid()<65534
            or len(sys.argv)!=2 or not sys.argv[1].isdigit() or not 3<=int(sys.argv[1])<1024):return 2
    drop_privileges()
    fd=int(sys.argv[1]);validate_lock(fd)
    # Every bounded wlr-randr request has its own parent-death notification and
    # retains the channel FD. Coordinator death cannot leave a late mode writer.
    worker=str(Path(__file__).with_name('worker.py'))
    def request(arguments):
        return subprocess.run([sys.executable,'-I',worker,str(os.getpid()),str(fd),'/usr/bin/wlr-randr',*arguments],
            stdin=subprocess.DEVNULL,capture_output=True,text=True,check=True,timeout=2,pass_fds=(fd,)).stdout
    session=Session()
    try:
        follower=Follower(request,session.state)
        while True:
            active=session.state()
            if active is None:return 0
            follower.tick(active);time.sleep(.25)
    finally:session.close()


if __name__=='__main__':raise SystemExit(main())
