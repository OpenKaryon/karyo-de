# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixed manager, PAM login and compositor checkpoint schema."""
import re
from .consoles import DESKTOP_VTS
from .journal_dns_record import identity
from .logind import LoginOwner


def invocation(value):
    if not isinstance(value,str) or re.fullmatch("[a-f0-9]{32}",value) is None:
        raise ValueError("Invalid session journal invocation")


def process(value):
    for key,limit in (("pid",2**31),("start",2**64)):
        if type(value[key]) is not int or not (1 if key=="pid" else 0)<value[key]<limit:
            raise ValueError("Invalid session journal process identity")
    invocation(value["invocation"])


def owner(value,account,*,manager=False,login_services=("karyo-desktop",)):
    if not isinstance(value,dict) or set(value)!={"id","name","uid","leader","kind","scope","service","state"}:
        raise ValueError("Invalid journal logind owner")
    if type(value["uid"]) is not int or type(value["leader"]) is not int:
        raise ValueError("Invalid journal logind numeric identity")
    mapping={"Id":value["id"],"Name":value["name"],"User":value["uid"],"Leader":value["leader"],
             "Class":value["kind"],"Scope":value["scope"],"Service":value["service"],"State":value["state"]}
    parsed=LoginOwner.parse("".join(k+"="+str(v)+"\n" for k,v in mapping.items()),value["id"])
    if (parsed.uid!=account.uid or parsed.name!=account.name or parsed.leader<=1
            or (parsed.kind not in {"manager","manager-early"} if manager else parsed.kind!="user")
            or parsed.service not in (("systemd-user",) if manager else login_services)
            or (bool(parsed.scope) if manager else not parsed.scope)):
        raise ValueError("The journal login belongs to another owner or service")
    return parsed


def validate_manager(manager,account,*,login_services=("karyo-desktop",)):
    """Validate the shared receipt without assuming how PAM was entered."""
    if not isinstance(manager,dict) or set(manager)!={"directory","file","dropins","live"}:
        raise ValueError("Invalid prepared manager receipt")
    identity(manager["directory"]);identity(manager["file"])
    paths=manager["dropins"]
    if (not isinstance(paths,list) or len(paths)>128 or any(not isinstance(p,str) or not p.startswith("/")
            or len(p)>4096 or any(c.isspace() for c in p) or "\\" in p or "%" in p for p in paths)
            or len(set(paths))!=len(paths)):
        raise ValueError("Invalid original user-manager policies")
    live=manager["live"]
    if live is None:return None
    if not isinstance(live,dict) or set(live)!={"invocation","pid","start","exe","login","manager_login"}:
        raise ValueError("Invalid live user-manager receipt")
    process(live);identity(live["exe"])
    login=owner(live["login"],account,login_services=login_services)
    manager_login=owner(live["manager_login"],account,manager=True)
    if manager_login.leader!=live["pid"] or manager_login.id==login.id:
        raise ValueError("Ambiguous native manager login identity")
    return login


def validate_session(value,account):
    if (not isinstance(value,dict) or not {"pending","manager","graphical"} <= set(value)
            or set(value) - {"pending","manager","graphical","catalog"}
            or "catalog" in value and value["catalog"] != "netbird"):
        raise ValueError("Invalid graphical session journal")
    pending=value["pending"]
    if pending is not None and (not isinstance(pending,str) or pending not in {
            "manager-prepare","login-start","enter","login-close","manager-close"}):
        raise ValueError("Invalid graphical session intent")
    manager=value["manager"];graph=value["graphical"]
    if manager is None:
        if pending!="manager-prepare" or graph is not None:raise ValueError("Missing prepared manager receipt")
        return
    login=validate_manager(manager,account)
    if pending=="manager-prepare":raise ValueError("Ambiguous manager preparation receipt")
    live=manager["live"]
    if live is None:
        if graph is not None or pending not in {None,"login-start","manager-close"}:
            raise ValueError("Missing live user-manager receipt")
        return
    if graph is None:
        if pending!="manager-close":raise ValueError("Missing graphical login receipt")
        return
    if not isinstance(graph,dict) or set(graph)!={"invocation","pid","start","login","scope_invocation","vt","waiter","compositor","entered"}:
        raise ValueError("Invalid graphical login receipt")
    process(graph);invocation(graph["scope_invocation"]);identity(graph["waiter"]);identity(graph["compositor"])
    graphical_login=owner(graph["login"],account)
    if (graphical_login!=login or login.leader!=graph["pid"] or graph["pid"]==live["pid"]
            or type(graph["vt"]) is not int or graph["vt"] not in DESKTOP_VTS or type(graph["entered"]) is not bool
            or pending not in {None,"enter","login-close"}):
        raise ValueError("The recorded graphical and manager logins disagree")
