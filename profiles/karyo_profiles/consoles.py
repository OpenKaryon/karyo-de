# SPDX-License-Identifier: GPL-3.0-or-later
"""Hold only a free console while the controlled desktop startup prepares it."""
import array
from contextlib import contextmanager
from dataclasses import dataclass
import fcntl
import os
import stat

from .logind import SessionError, Sessions

# Linux VT_GETSTATE is a 16-bit in-use mask. Keep the entire product pool
# inside its representable range and away from the ordinary login consoles.
DESKTOP_VTS = tuple(range(8, 13))


@dataclass(frozen=True)
class ConsoleState:
    active: int
    busy: frozenset

    @classmethod
    def parse(cls, active, mask):
        if (type(active) is not int or not 1 <= active <= 63
                or type(mask) is not int or not 0 <= mask <= 65535 or not mask & 1):
            raise SessionError("The kernel returned invalid virtual console state")
        return cls(active, frozenset({active} | {vt for vt in range(1, 16) if mask & (1 << vt)}))


class KernelConsoles:
    def __init__(self):
        self.control = None

    @staticmethod
    def _open(vt):
        fd = os.open("/dev/tty" + str(vt), os.O_RDWR | os.O_NOCTTY | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            info = os.fstat(fd)
            if not stat.S_ISCHR(info.st_mode) or os.major(info.st_rdev) != 4 or os.minor(info.st_rdev) != vt:
                raise SessionError("The virtual console path is not the expected native device")
            return fd
        except BaseException:
            os.close(fd)
            raise

    def __enter__(self):
        if os.getuid() != 0 or os.geteuid() != 0:
            raise PermissionError("Only the root desktop runtime reserves virtual consoles")
        if self.control is not None:
            raise RuntimeError("A virtual console controller is already open")
        self.control = self._open(0)
        return self

    def __exit__(self, *unused):
        if self.control is not None:
            fd, self.control = self.control, None
            os.close(fd)

    def state(self):
        if self.control is None:
            raise RuntimeError("The native virtual console controller is not open")
        values = array.array("H", [0, 0, 0])
        # Linux UAPI struct vt_stat: active VT, unused signal, in-use mask.
        fcntl.ioctl(self.control, 0x5603, values, True)
        return ConsoleState.parse(values[0], values[2])

    def open(self, vt):
        if type(vt) is not int or vt not in DESKTOP_VTS:
            raise ValueError("A desktop console must be in the fixed native pool")
        if vt in self.state().busy:
            raise SessionError("The selected virtual console became busy")
        from .display_manager import require_partition
        require_partition()
        return self._open(vt)

    @staticmethod
    def close(fd):
        os.close(fd)


@dataclass
class ConsoleLease:
    vt: int
    fd: int
    kernel: object
    sessions: object
    store: object
    _held: bool = True

    @property
    def path(self):
        return "/dev/tty" + str(self.vt)

    def verify(self):
        # An open descriptor keeps cooperating VT allocators away. It does not
        # authorize activation or let us displace a newly registered owner.
        if not self._held or not self.store._locked:
            raise PermissionError("The console reservation is no longer held under its configuration lock")
        occupied = self.sessions.occupied_vts()
        state = self.kernel.state()
        if self.vt == state.active or self.vt not in state.busy or self.vt in occupied:
            raise SessionError("The reserved virtual console ownership changed")


@contextmanager
def reserve_console(store, sessions=None, kernel=None):
    if not store._locked:
        raise PermissionError("Console reservation requires the profile configuration lock")
    sessions = Sessions() if sessions is None else sessions
    kernel = KernelConsoles() if kernel is None else kernel
    with kernel:
        occupied = sessions.occupied_vts() | kernel.state().busy
        vt = next((number for number in DESKTOP_VTS if number not in occupied), None)
        if vt is None:
            raise SessionError("Every desktop virtual console is in use")
        fd = kernel.open(vt)
        lease = ConsoleLease(vt, fd, kernel, sessions, store)
        try:
            lease.verify()
            yield lease
        finally:
            # Never activate, reset, hang up or disallocate somebody's console.
            lease._held = False
            kernel.close(fd)
