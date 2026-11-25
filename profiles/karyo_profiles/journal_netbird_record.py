# SPDX-License-Identifier: GPL-3.0-or-later
"""Provider receipts contain native identity, never opaque state or commands."""
from .journal_dns_record import identity, validate_unit

RESTART_PHASES = frozenset({'restart-start'})

def validate_provider(value):
    if not isinstance(value, dict) or set(value) != {"kind", "pending", "state", "service"} or value["kind"] != "netbird":
        raise ValueError("Unsupported profile provider receipt")
    pending = value["pending"]
    if pending is not None and (not isinstance(pending, str) or pending not in {"state-prepare", "service-start", "service-stop"} | RESTART_PHASES):
        raise ValueError("Invalid provider mutation intent")
    state = value["state"]
    if state is None:
        if pending != "state-prepare" or value["service"] is not None:
            raise ValueError("Missing provider state receipt")
        return
    if not isinstance(state, dict) or set(state) != {"directory", "stamp", "home"}:
        raise ValueError("Invalid provider state identity")
    for item in state.values(): identity(item)
    if pending == "state-prepare":
        raise ValueError("Ambiguous provider state receipt")
    service = value["service"]
    if service is None:
        if pending not in {'service-start', 'restart-start'}:
            raise ValueError("Missing provider service identity")
        return
    if not isinstance(service, dict) or set(service) != {"unit", "socket", "endpoint"}:
        raise ValueError("Invalid provider service receipt")
    validate_unit(service["unit"], "bus")
    identity(service["endpoint"])
    socket = service["socket"]
    if not isinstance(socket, list) or len(socket) != 4:
        raise ValueError("Invalid provider socket identity")
    identity(socket[:2]); identity(socket[2:])
    if socket[:2] != service["unit"]["runtime"][0]:
        raise ValueError("Provider socket and service runtime differ")
    if pending in {'service-start', 'restart-start'}:
        raise ValueError("Ambiguous provider service receipt")
