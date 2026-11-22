#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Text, PNG and file clipboard bridge between Xwayland and Wayland.

vmtoolsd -n vmusr exchanges the clipboard with the VMware host through the
X11 CLIPBOARD selection. labwc (wlroots) copies that selection to and from
Wayland only while an X11 window has keyboard focus, so Wayland applications
would not see host clipboard data. This bridge copies text, PNG and file lists in both
directions regardless of focus. The session agent runs it only in an active
Wayland session inside VMware.
"""
import ctypes
import ctypes.util
import hashlib
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time
from urllib.parse import unquote_to_bytes, urlsplit

LIMIT = 1 << 20
TEXT_TYPE = 'text/plain;charset=utf-8'
PNG_TYPE = 'image/png'
PNG_LIMIT = 32 << 20
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'
URI_TYPE = 'text/uri-list'
GNOME_TYPE = 'x-special/gnome-copied-files'
_owners = []
_owner_window = 0
_owner_timestamp = 0


def log(message):
    print(f'karyo-xclipboard: {message}', file=sys.stderr, flush=True)


def run(arguments, limit=LIMIT):
    """Read a selection with a bounded buffer and a finite transfer timeout."""
    process = None
    try:
        process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        chunks = []
        size = 0
        deadline = time.monotonic() + 5
        with selectors.DefaultSelector() as events:
            events.register(process.stdout, selectors.EVENT_READ)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not events.select(remaining):
                    return None
                chunk = os.read(process.stdout.fileno(), min(65536, limit + 1 - size))
                if not chunk:
                    return b''.join(chunks) if process.wait(timeout=max(.01, deadline - time.monotonic())) == 0 else None
                size += len(chunk)
                if size > limit:
                    return None
                chunks.append(chunk)
    except (OSError, subprocess.SubprocessError):
        return None
    finally:
        if process is not None:
            if process.poll() is None:
                process.kill()
            process.wait()
            process.stdout.close()


def file_uris(data):
    """Normalize local file URLs without opening VMware's blocking file paths."""
    try:
        lines = data.decode('utf-8').splitlines()
        if lines and lines[0] in ('copy', 'cut'):
            lines = lines[1:]
        uris = []
        for line in lines:
            if not line or line.startswith('#'):
                continue
            uri = urlsplit(line)
            if (uri.scheme != 'file' or uri.netloc not in ('', 'localhost')
                    or uri.query or uri.fragment):
                return None
            path = os.fsdecode(unquote_to_bytes(uri.path))
            if not path.startswith('/') or '\0' in path:
                return None
            uris.append(Path(path).as_uri())
        return ('\r\n'.join(uris) + '\r\n').encode('utf-8') if uris else None
    except (UnicodeError, ValueError):
        return None


def read_selection(types, read):
    if types is None:
        return None
    formats = types.decode(errors='replace').splitlines()
    if 'x-kde-passwordManagerHint' in formats:
        return None
    # File managers also publish path text. Preserve their file-copy meaning.
    for target in (URI_TYPE, GNOME_TYPE):
        if target in formats:
            data = read(target, LIMIT)
            uris = file_uris(data) if data is not None else None
            return (URI_TYPE, uris) if uris is not None else None
    if PNG_TYPE in formats:
        data = read(PNG_TYPE, PNG_LIMIT)
        if data is not None and data.startswith(PNG_SIGNATURE):
            return PNG_TYPE, data
        return None
    for target in (TEXT_TYPE, 'UTF8_STRING', 'text/plain'):
        if target in formats:
            data = read(target, LIMIT)
            return (TEXT_TYPE, data) if data is not None else None
    return None


def wayland_selection():
    return read_selection(run(['wl-paste', '--list-types'], limit=16384),
                          lambda target, limit: run(['wl-paste', '--no-newline', '--type', target], limit))


def x11_selection():
    return read_selection(run(['xclip', '-out', '-selection', 'clipboard', '-target', 'TARGETS'], limit=16384),
                          lambda target, limit: run(['xclip', '-out', '-selection', 'clipboard', '-target', target], limit))


def serve(arguments, data):
    # Selection servers fork; their children must not inherit captured pipes.
    try:
        return subprocess.run(arguments, input=data, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=5).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def start_x11_owner(selection):
    global _owner_window, _owner_timestamp
    mime, data = selection
    process = None
    try:
        helper = str(Path(__file__).with_name('owner.py'))
        process = subprocess.Popen([sys.executable, helper, mime], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        process.stdin.write(data)
        process.stdin.close()
        with selectors.DefaultSelector() as ready:
            ready.register(process.stdout, selectors.EVENT_READ)
            if not ready.select(5):
                raise RuntimeError('X11 clipboard owner did not become ready')
            reply = os.read(process.stdout.fileno(), 80).split()
            if len(reply) != 3 or reply[0] != b'READY':
                raise RuntimeError('X11 clipboard owner failed to acquire the selection')
            _owner_window, _owner_timestamp = int(reply[1]), int(reply[2])
        process.stdout.close()
        _owners.append(process)
        while len(_owners) > 4:
            previous = _owners.pop(0)
            if previous.poll() is None:
                previous.terminate()
            try: previous.wait(timeout=1)
            except subprocess.TimeoutExpired: previous.kill(); previous.wait()
        return True
    except (OSError, subprocess.SubprocessError, RuntimeError, ValueError) as error:
        log(str(error))
        if process is not None:
            if process.poll() is None:
                process.kill()
            process.wait()
            if process.stdin: process.stdin.close()
            if process.stdout: process.stdout.close()
        return False


def offer_x11(selection):
    # vmusr chooses between CLIPBOARD and PRIMARY using TIMESTAMP. An xclip
    # owner provides no timestamp and can lose to an older PRIMARY selection.
    return start_x11_owner(selection)


def offer_wayland(selection):
    mime, data = selection
    return serve(['wl-copy', '--type', mime], data)


def fingerprint(selection):
    mime, data = selection
    return mime, hashlib.sha256(data).digest()


def should_offer_x11(value, last, selection):
    if fingerprint(value) != last:
        return True
    if not _owner_window or selection.owner_id() != _owner_window:
        return False
    # Re-copying the same image is still a new Copy. Refresh its timestamp
    # when a newer PRIMARY selection would otherwise win in vmusr. A native
    # Xwayland echo with an older PRIMARY stays deduplicated, avoiding loops.
    helper = str(Path(__file__).with_name('owner.py'))
    stamp = run([sys.executable, helper, '--primary-timestamp'], limit=32)
    try:
        return stamp is not None and int(stamp) >= _owner_timestamp
    except ValueError:
        return False


class X11Selection:
    """XFixes notifications for CLIPBOARD owner changes."""

    def __init__(self):
        x11 = ctypes.CDLL(ctypes.util.find_library('X11') or 'libX11.so.6')
        xfixes = ctypes.CDLL(ctypes.util.find_library('Xfixes') or 'libXfixes.so.3')
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x11.XDefaultRootWindow.restype = ctypes.c_ulong
        x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x11.XInternAtom.restype = ctypes.c_ulong
        x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        x11.XConnectionNumber.argtypes = [ctypes.c_void_p]
        x11.XPending.argtypes = [ctypes.c_void_p]
        x11.XNextEvent.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        x11.XFlush.argtypes = [ctypes.c_void_p]
        x11.XGetSelectionOwner.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        x11.XGetSelectionOwner.restype = ctypes.c_ulong
        xfixes.XFixesQueryExtension.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int),
                                                ctypes.POINTER(ctypes.c_int)]
        xfixes.XFixesSelectSelectionInput.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                                      ctypes.c_ulong, ctypes.c_ulong]
        self.x11 = x11
        self.display = x11.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError('cannot open the X display')
        event_base = ctypes.c_int()
        error_base = ctypes.c_int()
        if not xfixes.XFixesQueryExtension(self.display, ctypes.byref(event_base), ctypes.byref(error_base)):
            x11.XCloseDisplay(self.display)
            self.display = None
            raise RuntimeError('the X server lacks XFIXES')
        # XFixesSelectionNotify is event 0 of the extension; mask bit 0 is
        # XFixesSetSelectionOwnerNotifyMask.
        self.notify = event_base.value
        clipboard = x11.XInternAtom(self.display, b'CLIPBOARD', 0)
        self.clipboard = clipboard
        xfixes.XFixesSelectSelectionInput(self.display, x11.XDefaultRootWindow(self.display), clipboard, 1)
        x11.XFlush(self.display)
        self.event = ctypes.create_string_buffer(192)

    def close(self):
        if self.display:
            self.x11.XCloseDisplay(self.display)
            self.display = None

    def fileno(self):
        return self.x11.XConnectionNumber(self.display)

    def owner_id(self):
        return self.x11.XGetSelectionOwner(self.display, self.clipboard)

    def owner_changed(self):
        changed = False
        while self.x11.XPending(self.display):
            self.x11.XNextEvent(self.display, self.event)
            if ctypes.c_int.from_buffer(self.event).value == self.notify:
                changed = True
        return changed


def main():
    for number in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(number, lambda *unused: sys.exit(0))
    if 'DISPLAY' not in os.environ or 'WAYLAND_DISPLAY' not in os.environ:
        log('needs both DISPLAY and WAYLAND_DISPLAY')
        return 1
    try:
        selection = X11Selection()
    except (OSError, RuntimeError) as error:
        log(str(error))
        return 1
    # Drain the notification transfer before printing. echo alone can close
    # its input while wl-paste is still writing a multi-megabyte PNG.
    notify = ('import os,select,sys,time\n'
              'deadline=time.monotonic()+5; remaining=33554433\n'
              'while remaining and time.monotonic()<deadline:\n'
              ' if not select.select([sys.stdin],[],[],max(0,deadline-time.monotonic()))[0]: break\n'
              ' block=os.read(sys.stdin.fileno(),min(65536,remaining))\n'
              ' if not block: break\n'
              ' remaining-=len(block)\n'
              'print(flush=True)')
    watcher = subprocess.Popen(['wl-paste', '--watch', sys.executable, '-c', notify], stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    selector = selectors.DefaultSelector()
    selector.register(selection, selectors.EVENT_READ, 'x11')
    selector.register(watcher.stdout, selectors.EVENT_READ, 'wayland')
    last = None
    try:
        while watcher.poll() is None:
            _owners[:] = [owner for owner in _owners if owner.poll() is None]
            for key, _ in selector.select(.5):
                if key.data == 'wayland':
                    if not os.read(watcher.stdout.fileno(), 4096):
                        return 1
                    value = wayland_selection()
                    if value is None:
                        last = None
                    elif should_offer_x11(value, last, selection) and offer_x11(value):
                        last = fingerprint(value)
            # Xlib queries can buffer XFixes events while consuming socket
            # readability. Drain that queue even without a new fd wakeup.
            # Handle Wayland first so an old X11 notification cannot restore
            # a clipboard value that a new guest Copy has just replaced.
            if selection.owner_changed():
                formats = run(['wl-paste', '--list-types'], limit=16384)
                if formats is not None and b'x-kde-passwordManagerHint' in formats.splitlines():
                    continue
                value = x11_selection()
                if value is None:
                    last = None
                elif fingerprint(value) != last and offer_wayland(value):
                    last = fingerprint(value)
    finally:
        if watcher.poll() is None:
            watcher.terminate()
            try: watcher.wait(timeout=3)
            except subprocess.TimeoutExpired: watcher.kill(); watcher.wait()
        watcher.stdout.close()
        selector.close()
        selection.close()
        for owner in _owners:
            if owner.poll() is None:
                owner.terminate()
            try:
                owner.wait(timeout=3)
            except subprocess.TimeoutExpired:
                owner.kill()
                owner.wait()
    return 0


if __name__ == '__main__':
    sys.exit(main())
