#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only identity acceptance, restricted to the two disposable QA guests."""
import json
import os
from pathlib import Path
import socket
import sys

if socket.gethostname() not in {"karyo-de-acceptance", "karyo-de-ubuntu-acceptance"} or os.geteuid() != 0:
    raise SystemExit("Run only as root on a disposable Karyo acceptance VM")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.credentials import LocalAccounts
from karyo_profiles.logind import Sessions, authorized_caller, listing, process_session
from karyo_profiles.storage import ConfigStore, atomic_write, trusted_directory

if sys.argv[1:] == ["--outside-session"]:
    # A root SSH process with forged GUI environment must still be denied.
    environment = {"PKEXEC_UID": "1001", "XDG_SESSION_ID": "c1", "USER": "karyoqa"}
    try:
        authorized_caller(environment)
    except PermissionError:
        print("PASS non-graphical SSH context denied despite forged GUI environment")
    else:
        raise AssertionError("A remote or non-graphical context became a desktop caller")
    raise SystemExit(0)
if sys.argv[1:]:
    raise SystemExit("Only the graphical probe or --outside-session is supported")
caller = authorized_caller(os.environ)
assert caller.name == "karyoqa" and caller.uid == 1001
session = Sessions().get(process_session())
assert session.active and session.uid == 1001
inventory = listing(caller, ConfigStore(), LocalAccounts(), Sessions())
assert any(item["current"] and item["active"] for item in inventory)
report = {"acceptance": "PASS actual polkit root helper in active graphical login", "session": session.id,
          "caller": caller.name, "uid": caller.uid, "inventory": inventory}
directory = trusted_directory(Path("/var/lib/karyo-qa"))
try:
    atomic_write(directory, "profile-logind-native.json", (json.dumps(report, indent=2) + "\n").encode())
finally:
    os.close(directory)
print(report["acceptance"])
