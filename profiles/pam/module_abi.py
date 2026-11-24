# SPDX-License-Identifier: GPL-3.0-or-later
"""Load the native PAM library without opening a PAM session or changing state."""
import ctypes
import os
import sys

library = ctypes.CDLL(sys.argv[1], mode=os.RTLD_NOW | os.RTLD_LOCAL)
for name in ('pam_sm_open_session', 'pam_sm_close_session'):
    if not callable(getattr(library, name)):
        raise RuntimeError('Missing PAM session entry point')
for name in ('pam_sm_authenticate', 'pam_sm_setcred', 'pam_sm_acct_mgmt', 'pam_sm_chauthtok'):
    if hasattr(library, name):
        raise RuntimeError('The session-only module exports an unrelated PAM entry point')
print('Native session-only PAM module loaded; no session operation was invoked')
