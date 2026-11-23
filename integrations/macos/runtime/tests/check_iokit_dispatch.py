#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real dispatcher body with ordinary distinct request handlers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--private-header", type=Path, required=True)
    parser.add_argument("--compiler", default="clang++")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.read_text()
    header = args.private_header.read_text()
    start = source.index("IOExternalMethod *IOSurfaceRoot::getExternalMethodForIndex(")
    end = source.index("IOReturn IOSurfaceRoot::createSurface(", start)
    body = source[start:end]
    start = header.index("enum\n{")
    selectors = header[start:header.index("};", start) + 2]
    handlers = ["createSurface", "releaseSurface", "lockSurface", "unlockSurface",
                "lookupById", "incrementSurfaceUseCount", "decrementSurfaceUseCount",
                "lookupByMachPort", "getSurfaceMachPort", "processUseCount"]
    fixture = '''#include <cstdio>
#include <cstdint>
class IOSurfaceRoot;
using UInt32 = uint32_t;
using IOMethod = void (IOSurfaceRoot::*)();
enum { kIOUCStructIStructO, kIOUCScalarIScalarO, kIOUCScalarIStructO,
       kIOUCVariableStructureSize = -1 };
struct IOExternalMethod {
    IOMethod func; int flags; int count0; int count1; IOSurfaceRoot *object;
};
'''
    fixture += selectors + "\nclass IOSurfaceRoot { public:\n int last = -1, calls = 0;\n"
    fixture += " void registerCaller() { ++calls; }\n"
    fixture += " IOExternalMethod *getExternalMethodForIndex(UInt32);\n"
    for number, name in enumerate(handlers):
        fixture += " void " + name + "() { last = " + str(number) + "; }\n"
    fixture += "};\n" + body
    fixture += '''int main() {
    IOSurfaceRoot first, second;
    const UInt32 requests[] = { kIOSurfaceMethodCreate, kIOSurfaceMethodLock,
        kIOSurfaceMethodLookupByID, kIOSurfaceMethodUnlock, kIOSurfaceMethodRelease };
    const int expected[] = { 0, 2, 4, 3, 1 };
    for (int i = 0; i != 5; ++i) {
        IOSurfaceRoot *owner = i % 2 ? &second : &first;
        IOExternalMethod *method = owner->getExternalMethodForIndex(requests[i]);
        if (!method || method->object != owner || !method->func) return 1;
        (owner->*(method->func))();
        if (owner->last != expected[i]) return 2;
    }
    if (first.calls != 3 || second.calls != 2) return 3;
    std::puts("ordinary IOKit request selection passed");
}
'''
    with tempfile.TemporaryDirectory(prefix="karyo-iokit-dispatch-") as temporary:
        work = Path(temporary)
        cpp, binary = work / "dispatch.cpp", work / "dispatch"
        cpp.write_text(fixture)
        subprocess.run([args.compiler, "-std=gnu++17", str(cpp), "-o", str(binary)], check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    record = {"probe": "ordinary-iokit-dispatch", "passed": run.returncode == 0,
              "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
              "body_sha256": hashlib.sha256(body.encode()).hexdigest(),
              "status": run.returncode, "stdout": run.stdout,
              "scope": "actual dispatcher body with stand-in request handlers",
              "full_iokit_ipc_accepted": False, "iosurface_buffer_accepted": False}
    args.output.write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record))
    return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
