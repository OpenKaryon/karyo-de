# SPDX-License-Identifier: GPL-3.0-or-later
"""Distinguish normal native stop progress from retained worker ownership."""
from .journal import JournalRetained
from .services import ServiceError


class WorkerPending(ServiceError):
    """The same captured worker is still completing its bounded transition."""


class WorkerRetained(JournalRetained):
    """Worker cleanup is not established; keep its journal and context."""
