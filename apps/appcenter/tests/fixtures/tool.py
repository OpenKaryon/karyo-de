#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Local software-manager fixture: no package manager, download or network."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

root = Path(os.environ["QA_SOFTWARE"])
program = Path(sys.argv[0]).name
args = sys.argv[1:]
with (root / "calls").open("a") as log:
    log.write(json.dumps([program, *args]) + "\n")
if (root / "fail-query").exists() and program in {"dpkg-query", "apt-cache", "apt-get"}:
    print("Query failed by QA", file=sys.stderr)
    sys.exit(2)
if program == "dpkg-query":
    print("qa-installed:amd64\tii \t1:1.0-2")
elif program == "apt-cache":
    if args[0] == "policy":
        candidate = "(none)" if (root / "no-candidate").exists() else "1:2.0-1"
        print(f"qa-installed:\n  Candidate: 1:1.0-3\nqa-new:\n  Candidate: {candidate}")
    else:
        print("qa-found - A public test package")
elif program == "apt-get":
    print("Inst qa-installed [1:1.0-2] (1:1.0-3 Debian-Security [amd64])")
elif program in {"pkexec", "conda"}:
    time.sleep(0.12)
    if (root / "deny-job").exists():
        print("Package denied by QA", file=sys.stderr)
        sys.exit(2)
    print("pmstatus:qa:50:Installing\n")
    (root / "job-completed").write_text("done")
elif program == "flatpak":
    if args[0] == "list":
        if "--user" not in args:
            sys.exit(8)
        print("org.example.QA")
    elif args[0] == "search":
        print("org.example.QA\tQA Flatpak\tPublic test app\t1.0")
    else:
        time.sleep(0.05)
        (root / "flatpak-completed").write_text("done")
elif program == "du":
    print("12345\t" + args[-1])
elif program == "karyo-terminal":
    (root / "terminal-started").write_text("yes")
    # Source only the generated rcfile, without starting an interactive shell.
    sys.exit(subprocess.run(["/bin/bash", "--noprofile", "--norc", "-c", '. "$1"', "fixture", args[-1]]).returncode)
elif program == "curl":
    if "-w" in args:
        print("https://github.com/conda-forge/miniforge/releases/tag/26.9.4-2", end="")
    else:
        target = Path(args[args.index("-o") + 1])
        payload = (root / "installer-script").read_bytes()
        if args[-1].endswith(".sha256"):
            digest = "0" * 64 if (root / "bad-sha").exists() else hashlib.sha256(payload).hexdigest()
            target.write_text(digest + "  Miniforge3-26.9.4-2-Linux-x86_64.sh\n")
        else:
            target.write_bytes(payload)
else:
    print("Unexpected fixture executable", file=sys.stderr)
    sys.exit(8)
