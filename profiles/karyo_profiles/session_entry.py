# SPDX-License-Identifier: GPL-3.0-or-later
"""Wait for the owned PAM session to become active before starting its GUI."""
import ctypes
import os
import sys
import time

from .logind import process_session


def wait_active(identifier, uid, *, library=None, clock=time.monotonic, sleep=time.sleep):
    library = ctypes.CDLL("libsystemd.so.0") if library is None else library
    library.sd_session_get_uid.argtypes = (ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint))
    library.sd_session_get_uid.restype = ctypes.c_int
    library.sd_session_is_active.argtypes = (ctypes.c_char_p,)
    library.sd_session_is_active.restype = ctypes.c_int
    key = identifier.encode("ascii")
    deadline = clock() + 60
    while True:
        current = ctypes.c_uint()
        if library.sd_session_get_uid(key, ctypes.byref(current)) < 0 or current.value != uid:
            raise PermissionError("The profile PAM session owner changed")
        active = library.sd_session_is_active(key)
        if active < 0:
            raise RuntimeError("The profile PAM session is unavailable")
        if active > 0:
            return
        if clock() >= deadline:
            raise TimeoutError("The profile was not entered before its startup deadline")
        sleep(0.05)


def main(program):
    try:
        uid = os.getuid()
        if sys.argv[1:] or uid != os.geteuid() or not 1000 <= uid < 65534:
            raise PermissionError("The profile entry requires one ordinary local account")
        wait_active(process_session(), uid)
        # No loginctl activation or user-sourced environment here. The root
        # controller confirms source privacy and activates the owned session.
        os.execv(program, [program])
    except (OSError, ValueError, PermissionError, RuntimeError) as error:
        print("karyo-profile-session: " + str(error), file=sys.stderr)
        return 1
