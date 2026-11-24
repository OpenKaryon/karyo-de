# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe sustained kernel waits in the coordinator's captured vmusr group."""
from dataclasses import dataclass
from pathlib import Path
import sys

SAMPLE_SECONDS = 5.0
BLOCKED_SECONDS = 20.0


def log(message):
    print('karyo-vmware-agent: ' + message, file=sys.stderr, flush=True)
    try:
        import syslog
        syslog.openlog('karyo-vmware-agent')
        syslog.syslog(syslog.LOG_WARNING, message)
    except (ImportError, OSError):
        pass


@dataclass(frozen=True)
class ProcessWait:
    pid: int
    start: int
    state: str
    channel: str


def vmusr_wait(group, proc=Path('/proc')):
    for path in sorted((p for p in proc.iterdir() if p.name.isdecimal()),
                       key=lambda p: int(p.name)):
        try:
            fields = (path / 'stat').read_text().rpartition(')')[2].split()
            if int(fields[2]) != group:
                continue
            args = (path / 'cmdline').read_bytes().split(b'\0')
            if args[0].rsplit(b'/', 1)[-1] != b'vmtoolsd':
                continue
            if b'vmusr' not in args[1:] and b'--name=vmusr' not in args[1:]:
                continue
            try:
                channel = (path / 'wchan').read_text().strip()
            except OSError:
                channel = ''
            return ProcessWait(int(path.name), int(fields[19]), fields[0], channel)
        except (OSError, ValueError, IndexError):
            continue
    return None


class WaitMonitor:
    def __init__(self, reader=vmusr_wait, report=log):
        self.reader = reader
        self.report = report
        self.group = None
        self.identity = None
        self.blocked_since = None
        self.reported = False
        self.next_sample = 0.0

    def poll(self, group, now):
        if group != self.group or group is None:
            self.group = group
            self.identity = self.blocked_since = None
            self.reported = False
            self.next_sample = now
        if group is None or now < self.next_sample:
            return
        self.next_sample = now + SAMPLE_SECONDS
        process = self.reader(group)
        identity = (process.pid, process.start) if process is not None else None
        if identity != self.identity:
            self.identity = identity
            self.blocked_since = None
            self.reported = False
        if process is None or process.state != 'D':
            if self.reported:
                self.report(f'vmusr PID {process.pid} left an uninterruptible kernel wait')
            self.blocked_since = None
            self.reported = False
            return
        if self.blocked_since is None:
            self.blocked_since = now
        elif not self.reported and now - self.blocked_since >= BLOCKED_SECONDS:
            self.report(f'vmusr PID {process.pid} remains in uninterruptible kernel sleep '
                        f'(D) after {int(now - self.blocked_since)}s; '
                        f'wait channel: {process.channel or "unknown"}')
            self.reported = True
