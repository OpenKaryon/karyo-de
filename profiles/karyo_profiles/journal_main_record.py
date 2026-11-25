# SPDX-License-Identifier: GPL-3.0-or-later
"""Receipt for a main login entered by its original privileged PAM parent."""
import re

from .journal_dns_record import identity
from .journal_session_record import owner, validate_manager


def validate_origin(value):
    if (not isinstance(value, dict)
            or set(value) != {"pid", "start", "exe", "cgroup", "service"}):
        raise ValueError("Invalid main PAM parent receipt")
    for key, minimum, maximum in (("pid", 1, 2**31), ("start", 0, 2**64)):
        if type(value[key]) is not int or not minimum < value[key] < maximum:
            raise ValueError("Invalid main PAM process identity")
    identity(value["exe"])
    if (not isinstance(value["cgroup"], str) or len(value["cgroup"]) > 255
            or re.fullmatch(r"/system\.slice/[A-Za-z0-9_-]+\.service", value["cgroup"]) is None
            or not isinstance(value["service"], str)
            or re.fullmatch(r"[A-Za-z0-9_-]{1,64}", value["service"]) is None):
        raise ValueError("Invalid main PAM entry service")


def validate_main(value, account):
    required = {'pending','origin','manager','login'}
    if (not isinstance(value, dict) or not required <= set(value) or set(value)-required-{'bus_view','network_client'}
            or 'network_client' in value and value['network_client'] is not True
            or 'bus_view' in value and value['bus_view'] != 'host-proxy'):
        raise ValueError("Invalid main login receipt")
    validate_origin(value["origin"])
    pending, manager, login = value["pending"], value["manager"], value["login"]
    if pending is not None and (not isinstance(pending, str)
            or pending not in {"manager-prepare", "entry-start", "login-close", "manager-close"}):
        raise ValueError("Invalid main login transition")
    if manager is None:
        if pending != "manager-prepare" or login is not None:
            raise ValueError("Missing prepared main manager receipt")
        return
    if pending == "manager-prepare":
        raise ValueError("Ambiguous main manager preparation")
    services = (value["origin"]["service"],)
    captured = validate_manager(manager, account, login_services=services)
    if captured is None:
        if login is not None or pending not in {"entry-start", "login-close", "manager-close"}:
            raise ValueError("Missing live main manager receipt")
        return
    parsed = owner(login, account, login_services=services)
    if (parsed != captured or parsed.leader != value["origin"]["pid"]
            or pending == "entry-start"):
        raise ValueError("The main login and manager receipts disagree")
