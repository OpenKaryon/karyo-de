#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Open/hold/release QA consoles without activation, reset or session startup."""
import hashlib
import json
import os
from pathlib import Path
import socket
import sys
import time

if os.getuid() != 0 or os.geteuid() != 0 or socket.gethostname() not in {
        "karyo-de-acceptance", "karyo-de-ubuntu-acceptance"}:
    raise SystemExit("Use only the disposable acceptance hosts as root")
sys.dont_write_bytecode = True
sys.path.insert(0, "/usr/libexec/karyo/profiles")
from karyo_profiles.consoles import KernelConsoles, reserve_console
from karyo_profiles.logind import Sessions
from karyo_profiles.storage import ConfigStore

paths = [Path("/etc/passwd"), Path("/etc/shadow"), Path("/etc/group"),
         *sorted(Path("/etc/pam.d").glob("common-*"))]
before_hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
sessions = Sessions()
initial = sessions.all()
main = [row for row in initial if row.name == "karyoqa" and row.uid == 1001 and row.active]
assert len(main) == 1
store = ConfigStore()
with store.locked(), KernelConsoles() as kernel:
    before = kernel.state()
    def settled(predicate):
        started = time.monotonic()
        while True:
            current = kernel.state()
            assert current.active == before.active
            if predicate(current):
                return round((time.monotonic() - started) * 1000, 3)
            if time.monotonic() - started >= 2:
                raise TimeoutError("Closed QA console remained busy beyond its release deadline")
            time.sleep(0.005)
    assert before.active == main[0].vt
    owners = sessions.occupied_vts()
    with reserve_console(store, sessions) as first:
        first.verify()
        assert first.vt not in before.busy and first.vt not in owners
        assert not os.get_inheritable(first.fd)
        assert os.major(os.fstat(first.fd).st_rdev) == 4
        assert os.minor(os.fstat(first.fd).st_rdev) == first.vt
        held = kernel.state()
        assert held.active == before.active and first.vt in held.busy
        with reserve_console(store, sessions) as second:
            second.verify()
            first.verify()
            assert second.vt != first.vt and second.vt not in before.busy and second.vt not in owners
            assert not os.get_inheritable(second.fd)
            assert kernel.state().busy >= {first.vt, second.vt}
            second_vt = second.vt
        second_release_ms = settled(lambda state: second_vt not in state.busy and first.vt in state.busy)
        first_vt = first.vt
    final_release_ms = settled(lambda state: state == before)
    try:
        first.verify()
        raise AssertionError("A released reservation was reused")
    except PermissionError:
        pass
    print(json.dumps({"active": before.active, "before_busy": sorted(before.busy),
                      "owners": sorted(owners), "first": first_vt, "second": second_vt,
                      "after_busy": sorted(kernel.state().busy),
                      "second_release_ms": second_release_ms, "final_release_ms": final_release_ms}))
assert sessions.all() == initial
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == value for p, value in before_hashes.items())
print("PASS two native VT descriptors reserve distinct free consoles and release them; active session and account/PAM bytes unchanged")
