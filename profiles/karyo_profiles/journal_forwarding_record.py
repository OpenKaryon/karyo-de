# SPDX-License-Identifier: GPL-3.0-or-later
"""Typed route/rule receipts and comparison-only native nft JSON."""
import ipaddress
from types import SimpleNamespace

from .forwarding import PRIVATE_POOL,ProfileForwarding,_table
from .underlay import PhysicalUnderlay


def physical(value):
    if not isinstance(value,dict) or set(value)!={"interface","index","address","prefix","gateway","metric"}:
        raise ValueError("Invalid forwarding underlay receipt")
    return PhysicalUnderlay(**value)


def native_json(value,depth=0):
    if depth>16:raise ValueError("Forwarding snapshot nesting exceeds its limit")
    if value is None or type(value) in {bool,int}:return
    if isinstance(value,str) and len(value)<=4096 and not any(ord(c)<32 for c in value):return
    if isinstance(value,list) and len(value)<=256:
        for item in value:native_json(item,depth+1)
        return
    if isinstance(value,dict) and len(value)<=32:
        for key,item in value.items():
            if not isinstance(key,str) or not key or len(key)>128:raise ValueError("Invalid native forwarding key")
            native_json(item,depth+1)
        return
    raise ValueError("Invalid bounded native forwarding JSON")


def validate_forwarding(value,spec):
    if not isinstance(value,dict) or set(value)!={"physical","pending","proof"}:
        raise ValueError("Invalid forwarding journal record")
    underlay=physical(value["physical"])
    if ipaddress.IPv4Network(underlay.subnet).overlaps(PRIVATE_POOL):raise ValueError("Physical underlay overlaps profile addresses")
    gateway=ProfileForwarding(SimpleNamespace(spec=spec,command=None),underlay)
    pending=value["pending"]
    if pending is not None and (not isinstance(pending,str) or pending not in {"create","remove"}):
        raise ValueError("Invalid forwarding journal intent")
    proof=value["proof"]
    if proof is None:
        if pending!="create":raise ValueError("Missing forwarding ownership receipt")
        return
    if pending=="create" or not isinstance(proof,dict) or set(proof)!={"priorities","routes","rules","firewall"}:
        raise ValueError("Invalid forwarding ownership receipt")
    priorities=proof["priorities"]
    if (not isinstance(priorities,list) or len(priorities)!=2 or any(type(v) is not int for v in priorities)
            or not 1<=priorities[0]<=16 or not 17<=priorities[1]<=32):
        raise ValueError("Invalid forwarding rule priorities")
    routes=proof["routes"];rules=proof["rules"]
    if not isinstance(routes,list) or len(routes)>4 or not isinstance(rules,list) or len(rules)>2:
        raise ValueError("Invalid forwarding route/rule count")
    plan=gateway.route_plan()
    for index,row in enumerate(routes):
        fields=plan[index][1]
        if (not isinstance(row,dict) or not (set(fields)|{"table","protocol"})<=set(row)
                or set(row)-(set(fields)|{"type","scope","table","protocol","flags"})
                or _table(row["table"])!=gateway.table or row["protocol"] not in (4,"4","static")
                or any(row.get(k)!=v for k,v in fields.items()) or row.get("flags",[])!=[]):
            raise ValueError("The captured route is not its fixed profile plan")
        if "type" not in fields and (type(row.get("type","unicast")) not in {int,str} or row.get("type","unicast") not in (1,"1","unicast")):
            raise ValueError("Unexpected unicast profile route type")
        if "scope" not in fields and (type(row.get("scope","global")) not in {int,str} or row.get("scope","global") not in (0,"0","global")):
            raise ValueError("Unexpected global profile route scope")
    for index,row in enumerate(rules):
        expected={"priority":priorities[index],"src":spec.guest if index==0 else "all","table":str(gateway.table)}
        if index==1:expected["dst"]=spec.guest
        if not isinstance(row,dict) or type(row.get("priority")) is not int:raise ValueError("Invalid native forwarding rule")
        normalized=dict(row)
        if "table" in row:normalized["table"]=str(_table(row["table"]))
        for key in ("src","dst"):
            if key in normalized and normalized[key]!="all":
                if not isinstance(normalized[key],str):raise ValueError("Invalid native profile rule address")
                address=ipaddress.IPv4Network(normalized[key],strict=False)
                if address.prefixlen!=32:raise ValueError("A forwarding rule requires its exact profile address")
                normalized[key]=str(address.network_address)
        if normalized!=expected:raise ValueError("The forwarding rule is not its fixed profile selector")
    firewall=proof["firewall"]
    if firewall is not None:
        if (not isinstance(firewall,dict) or set(firewall)!={"handle","snapshot"}
                or type(firewall["handle"]) is not int or not 0<firewall["handle"]<2**64
                or not isinstance(firewall["snapshot"],list) or not 1<=len(firewall["snapshot"])<=32):
            raise ValueError("Invalid native firewall receipt")
        native_json(firewall["snapshot"])
        tables=[]
        for item in firewall["snapshot"]:
            if not isinstance(item,dict) or len(item)!=1 or not set(item)<={"table","chain","rule"}:
                raise ValueError("Unexpected native forwarding firewall object")
            kind,row=next(iter(item.items()))
            if not isinstance(row,dict) or row.get("family")!="inet":raise ValueError("Invalid native firewall family")
            if kind=="table":
                if row.get("name")!=gateway.name or row.get("comment")!=spec.alias or type(row.get("handle")) is not int or row.get("handle")!=firewall["handle"]:
                    raise ValueError("Native firewall ownership differs from its profile")
                tables.append(row)
            elif row.get("table")!=gateway.name:raise ValueError("Another native firewall table was captured")
        if len(tables)!=1:raise ValueError("A forwarding receipt needs exactly one native table")
    if ((len(rules)>0 or firewall is not None) and len(routes)!=4
            or len(rules)>0 and firewall is None or pending is None and (len(routes)!=4 or len(rules)!=2 or firewall is None)):
        raise ValueError("Forwarding removal is out of dependency order")
