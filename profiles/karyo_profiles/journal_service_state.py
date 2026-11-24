# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture persistent service state without storing its credentials."""
import os

from .journal import JournalRetained
from .storage import trusted_directory


def state_receipt(state):
    state.verify()
    info = os.stat('owner.json', dir_fd=state.fd, follow_symlinks=False)
    return {'directory': list(state.identity), 'stamp': [info.st_dev, info.st_ino],
            'home': list(state.home_identity)}


def restore_state(state, proof):
    state.home_identity = tuple(proof['home'])
    state.owner = state.owner_contents(state.home_identity)
    try:
        state.fd = trusted_directory(state.path)
        state.identity = tuple(proof['directory'])
        if state_receipt(state) != proof:
            raise JournalRetained('The captured service state or ownership stamp changed')
    except BaseException:
        state.release_handles()
        raise
