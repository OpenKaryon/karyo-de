#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Disposable nmcli fixture. No network commands or real configuration changes."""
import json
import os
from pathlib import Path
import sys
import time

root = Path(os.environ["QA_NM"])
args = sys.argv[1:]
with (root / "calls").open("a") as log:
    log.write(json.dumps(args) + "\n")

if (root / "offline").exists():
    print("NetworkManager unavailable", file=sys.stderr)
    sys.exit(3)

if args[:2] == ["radio", "wifi"]:
    if len(args) == 2:
        print((root / "radio").read_text())
        sys.exit()
    time.sleep(0.1)
    if (root / "deny").exists():
        print("Radio denied", file=sys.stderr)
        sys.exit(2)
    (root / "radio").write_text("enabled" if args[2] == "on" else "disabled")
    sys.exit()

if "NAME,UUID,TYPE,DEVICE" in args:
    print(r"Office\:East:12345678-1234-1234-1234-123456789012:802-11-wireless:--")
    sys.exit()
if "TYPE" in args:
    print("wifi\nethernet")
    sys.exit()
if "IN-USE,SSID,SIGNAL,SECURITY,DEVICE,DBUS-PATH" in args:
    print(r":Office\:East:75:WPA2:wlan0:/org/freedesktop/NetworkManager/AccessPoint/1")
    print(r":Guest:55:WPA2:wlan0:/org/freedesktop/NetworkManager/AccessPoint/2")
    sys.exit()
if any(field.startswith("GENERAL.DEVICE,") for field in args):
    print("\n".join(("GENERAL.DEVICE:wlan0", "GENERAL.TYPE:wifi", "GENERAL.STATE:100 (connected)",
                     r"GENERAL.CONNECTION:Office\:East", "IP4.DNS[1]:1.1.1.1",
                     "IP4.DNS[2]:9.9.9.9", r"IP6.ADDRESS[1]:2001\:db8\:\:1/64", "")))
    sys.exit()
if "connection" in args and ("up" in args or "down" in args):
    time.sleep(0.1)
    if (root / "deny").exists():
        print("Connection denied", file=sys.stderr)
        sys.exit(2)
    sys.exit()

print("Unexpected fixture command", file=sys.stderr)
sys.exit(8)
