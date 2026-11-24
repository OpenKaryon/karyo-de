# SPDX-License-Identifier: GPL-3.0-or-later
"""Typed private DNS receipts; paths and service names are always derived."""
from dataclasses import asdict
import re

from .dns import DNSSeed,ResolverPlan
from .underlay import PhysicalUnderlay


def identity(value):
    if (not isinstance(value,list) or len(value)!=2 or any(type(v) is not int or not 0<v<2**64 for v in value)):
        raise ValueError("Invalid DNS journal kernel identity")


def seed(value):
    if not isinstance(value,dict) or set(value)!={"physical","servers","search"}:
        raise ValueError("Invalid DNS journal physical seed")
    if not isinstance(value["physical"],dict) or set(value["physical"])!={"interface","index","address","prefix","gateway","metric"}:
        raise ValueError("Invalid DNS journal physical interface")
    if not isinstance(value["servers"],list) or not isinstance(value["search"],list):
        raise ValueError("Invalid DNS journal seed lists")
    return DNSSeed(PhysicalUnderlay(**value["physical"]),tuple(value["servers"]),tuple(value["search"]))


def validate_unit(value,role):
    if not isinstance(value,dict) or set(value)!={"invocation","pid","start","exe","namespace","runtime"}:
        raise ValueError("Invalid DNS journal unit receipt")
    if not isinstance(value["invocation"],str) or re.fullmatch("[a-f0-9]{32}",value["invocation"]) is None:
        raise ValueError("Invalid DNS journal unit invocation")
    if type(value["pid"]) is not int or not 1<value["pid"]<2**31 or type(value["start"]) is not int or not 0<value["start"]<2**64:
        raise ValueError("Invalid DNS journal process identity")
    identity(value["exe"]);identity(value["namespace"])
    if not isinstance(value["runtime"],list) or len(value["runtime"])!=(1 if role=="bus" else 2):
        raise ValueError("Invalid DNS journal runtime directories")
    for item in value["runtime"]:identity(item)


def validate_dns(value,spec,gid,*,routing=False):
    required={"seed","service_uid","gid","pending","files","bus","resolver"}
    if not isinstance(value,dict) or not required<=set(value) or set(value)-required-{"refresh","view"}:
        raise ValueError("Invalid DNS journal record")
    # ResolverPlan validates scalar identities without accepting a recorded path.
    ResolverPlan(spec,seed(value["seed"]),value["gid"],value["service_uid"],"/run/karyo/validation-only",
                 value.get("view","resolver"))
    if "view" in value and value["view"] != "network-manager":
        raise ValueError("Invalid private DNS service view receipt")
    if value["gid"]!=gid:
        raise ValueError("The DNS journal profile group changed")
    pending=value["pending"]
    if pending is not None and (not isinstance(pending,str) or pending not in {
            "files-create","files-remove","bus-start","bus-stop","resolver-start","resolver-stop"}):
        raise ValueError("Invalid DNS journal mutation intent")
    files=value["files"]
    if files is None:
        if pending!="files-create" or value["bus"] is not None or value["resolver"] is not None:
            raise ValueError("Missing DNS journal file receipt")
    else:
        if not isinstance(files,dict) or set(files)!={"directory","files"} or not isinstance(files["files"],dict) or set(files["files"])!={"bus.conf","resolved.conf"}:
            raise ValueError("Invalid DNS journal configuration receipt")
        identity(files["directory"])
        for item in files["files"].values():identity(item)
        if pending=="files-create":raise ValueError("Ambiguous DNS journal file creation")
    for role in ("bus","resolver"):
        if value[role] is not None:validate_unit(value[role],role)
    if value["resolver"] is not None and value["bus"] is None:
        raise ValueError("A journal resolver requires its recorded private bus")
    if pending=="files-remove" and (value["bus"] is not None or value["resolver"] is not None):
        raise ValueError("DNS services must close before configuration")
    for role in ("bus","resolver"):
        if pending==role+"-start" and value[role] is not None or pending==role+"-stop" and value[role] is None:
            raise ValueError("Ambiguous DNS journal unit mutation")
    if pending=="bus-stop" and value["resolver"] is not None:
        raise ValueError("The resolver must close before its bus")
    if "refresh" in value:
        from .dns_refresh import validate_refresh
        if pending is not None or value["bus"] is None or value["resolver"] is None:
            raise ValueError("DNS refresh requires its acknowledged live services")
        validate_refresh(value["refresh"],seed(value["seed"]),routing=routing)
