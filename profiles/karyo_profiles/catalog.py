# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed, provider-only XDG catalog publication for captured desktop owners."""
import os
from pathlib import Path
from .model import root_file
from .storage import trusted_directory

NETBIRD_DATA = "/usr/share/karyo/profile-catalog/netbird"
NETBIRD_DESKTOP = "org.wails.netbird.desktop"
DATA_ENVIRONMENT = "XDG_DATA_DIRS=" + NETBIRD_DATA + ":/usr/local/share:/usr/share"


def require_netbird_catalog(directory=NETBIRD_DATA):
    """Require packaged root-owned data; never create or adopt user launchers."""
    fd = trusted_directory(Path(directory) / "applications")
    try:
        value = root_file(NETBIRD_DESKTOP, dir_fd=fd)
        if not value.startswith("[Desktop Entry]\n"):
            raise ValueError("The private provider desktop entry is invalid")
    finally:
        os.close(fd)
