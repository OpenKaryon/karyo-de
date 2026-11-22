# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary Wayland clipboard adapter; no network or administrator operations."""
import fcntl
import hashlib
import os
from pathlib import Path
import select
import signal
import socket
import stat
import subprocess
import sys
import time

from .clipboard import LIMIT, SOCKET, TYPES, ZERO, parse_response, receive, request, send, socket_path

PASTE = '/usr/bin/wl-paste'
COPY = '/usr/bin/wl-copy'


def bounded_output(arguments, limit, timeout=2):
    with subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL) as child:
        deadline = time.monotonic()+timeout
        data = bytearray()
        try:
            while len(data) <= limit:
                remaining = deadline-time.monotonic()
                if remaining <= 0 or not select.select([child.stdout], [], [], remaining)[0]:
                    raise TimeoutError('Clipboard producer did not finish')
                block = os.read(child.stdout.fileno(), min(65536, limit+1-len(data)))
                if not block:
                    return child.wait(timeout=max(.001,deadline-time.monotonic())), bytes(data)
                data.extend(block)
            raise ValueError('Clipboard selection exceeds its bound')
        finally:
            if child.poll() is None:
                child.kill()
            child.wait()


def clipboard():
    code, formats = bounded_output([PASTE, '--list-types'], 65536)
    if code:
        return False, None
    formats = formats.splitlines()
    if b'x-kde-passwordManagerHint' in formats:
        return True, None
    if b'image/png' in formats:
        target = mime = b'image/png'
    else:
        mime = TYPES[0]
        target = next((kind for kind in (mime,b'UTF8_STRING',b'text/plain') if kind in formats),None)
        if target is None:
            return True, None  # Unsupported selections stay local and are not overwritten.
    try:
        code, data = bounded_output([PASTE, '--no-newline', '--type', target.decode()], LIMIT)
    except (ValueError, TimeoutError):
        return True, None
    return (False,mime+b'\0'+data) if code == 0 else (True,None)


def write_clipboard(value):
    if value:
        mime, unused, data = value.partition(b'\0')
        arguments = [COPY, '--type', mime.decode()]
    else:
        arguments, data = [COPY, '--clear'], b''
    subprocess.run(arguments,input=data,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=2,check=True)


class Synchronizer:
    def __init__(self):
        self.known = ZERO
        self.previous = None
        self.initial = True
        self.seed = False

    def message(self,current,blocked,changed,clear=False):
        operation = ord('G')
        value = b''
        if not blocked:
            if clear:
                operation = ord('C')
            elif current is not None and (self.seed or changed and current != self.previous):
                genuine = changed and current != self.previous
                operation = ord('S') if self.initial or (self.seed and not genuine) else ord('U')
                value = current
        return request(operation,self.known,value)

    def newer_local_copy(self,before,after):
        if after is not None and after != before:
            # A genuine copy after this request started is newer than its
            # remote reply, including the first response to startup seeding.
            self.initial,self.seed,self.previous = False,False,before
            return True
        return False

    def accept(self,response,current,blocked,writer=write_clipboard):
        kind,revision,value = parse_response(response)
        if kind == b'E':
            self.known,self.initial,self.seed = ZERO,True,False
            return current
        self.initial = False
        if blocked:
            self.previous,self.seed = current,False
            return current
        if kind == b'Z':
            self.known,self.previous,self.seed = ZERO,current,current is not None
            return current
        if kind == b'D':
            if value != current:
                writer(value)  # A failed local write never acknowledges a remote revision.
            current = value
        elif kind == b'C':
            if current is not None:
                writer(b'')
            current = None
        self.known,self.previous,self.seed = revision,current,False
        return current


def notify():
    # Drain only a bounded amount/time; never print selection bytes or MIME names.
    deadline = time.monotonic()+2
    remaining = LIMIT+1
    while remaining and time.monotonic()<deadline:
        if not select.select([sys.stdin],[],[],max(0,deadline-time.monotonic()))[0]:
            break
        block = os.read(sys.stdin.fileno(),min(65536,remaining))
        if not block:
            break
        remaining -= len(block)
    state = os.environ.get('CLIPBOARD_STATE','data')
    print({'sensitive':'S','nil':'N','clear':'C'}.get(state,'D'),flush=True)


def singleton():
    uid = os.getuid()
    runtime = Path('/run/user')/str(uid)
    if os.environ.get('XDG_RUNTIME_DIR') != str(runtime):
        raise ValueError('Clipboard runtime does not match the real user')
    info = runtime.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != uid or stat.S_IMODE(info.st_mode) != 0o700:
        raise PermissionError('Clipboard requires the private native user runtime')
    display = os.environ.get('WAYLAND_DISPLAY','')
    if not display:
        return None
    name = 'karyo-clipboard-'+hashlib.sha256(display.encode()).hexdigest()+'.lock'
    fd = os.open(runtime/name,os.O_RDWR|os.O_CREAT|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK,0o600)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != uid or stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1:
            raise PermissionError('Clipboard client lock changed')
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        return fd
    except BlockingIOError:
        os.close(fd)
        return None
    except BaseException:
        os.close(fd)
        raise


def sync(watcher):
    state = Synchronizer()
    current,blocked,dirty,clear,sensitive = None,False,True,False,False
    while watcher.poll() is None:
        try:
            ready,unused,unused = select.select([watcher.stdout],[],[],.35)
            if ready:
                changes = os.read(watcher.stdout.fileno(),4096)
                if not changes:
                    return
                dirty = True
                last = changes.splitlines()[-1] if changes.splitlines() else b'D'
                clear,sensitive = last == b'C',last == b'S'
            socket_path()
            if dirty:
                blocked,current = (True,None) if sensitive else clipboard()
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
                connection.settimeout(3)
                connection.connect(SOCKET)
                send(connection,state.message(current,blocked,dirty,clear))
                response = receive(connection)
            if response[1:2] in (b'D',b'C') and not blocked:
                # A sensitive or unsupported selection may have appeared while
                # the remote request was in flight or its notifier was draining.
                latest_blocked,latest = clipboard()
                if state.newer_local_copy(current,latest):
                    current,blocked,dirty,clear = latest,latest_blocked,True,False
                    continue
                blocked,current = latest_blocked,latest
            current = state.accept(response,current,blocked)
            dirty,clear = False,False
            if response[1:2] == b'E':
                time.sleep(1.5)
        except (OSError,ValueError,subprocess.SubprocessError):
            time.sleep(.35)


def client():
    fd = singleton()
    if fd is None:
        return
    try:
        wrapper = str(Path(sys.argv[0]).resolve())
        with subprocess.Popen([PASTE,'--watch',sys.executable,'-I',wrapper,'--notify'],stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE,stderr=subprocess.DEVNULL) as watcher:
            try:
                sync(watcher)
            finally:
                if watcher.poll() is None:
                    watcher.terminate()
                try:watcher.wait(timeout=2)
                except subprocess.TimeoutExpired:watcher.kill();watcher.wait()
    finally:
        os.close(fd)


def main():
    if sys.argv[1:] == ['--serve']:
        from .clipboard import serve
        try:serve()
        except (OSError,ValueError,PermissionError):
            print('karyo-profile-clipboard: fixed unprivileged listener is unavailable.',file=sys.stderr)
            return 1
        return 0
    if (os.getuid()!=os.geteuid() or os.getgid()!=os.getegid() or not 1000<=os.getuid()<65534):
        print('karyo-profile-clipboard: an ordinary desktop account is required.',file=sys.stderr)
        return 1
    if sys.argv[1:] == ['--notify']:
        notify()
        return 0
    if sys.argv[1:]:
        print('Usage: karyo-profile-clipboard [--serve]',file=sys.stderr)
        return 2
    signal.signal(signal.SIGTERM,lambda *unused: sys.exit(0))
    try:client()
    except (OSError,ValueError):
        print('karyo-profile-clipboard: private Wayland clipboard is unavailable.',file=sys.stderr)
        return 1
    return 0
