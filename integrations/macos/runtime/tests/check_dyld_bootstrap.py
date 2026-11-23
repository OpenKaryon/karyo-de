#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual patched dyld entry body with functional startup callbacks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    source=args.source.read_text()
    signature='uintptr_t start(const dyld3::MachOLoaded* appsMachHeader, int argc, const char* argv[],\n'
    attribute='__attribute__((no_stack_protector))\n'
    if source.count(attribute+signature)!=1:
        raise ValueError('Expected the reviewed attribute on exactly the actual bootstrap entry')
    body=attribute+signature+source.split(attribute+signature,1)[1].split('\n\n#if TARGET_OS_SIMULATOR',1)[0]
    args.output.mkdir(parents=True,exist_ok=False)
    # The callbacks model the entry contract; no deliberately rejected operation
    # or negative privilege/protection scenario is part of this functional check.
    prefix=r"""#include <cstdint>
#include <cstdio>
#include <cstring>
#define DARLING 1
#define DYLD_INITIALIZER_SUPPORT 0
#define DBG_DYLD_TIMING_BOOTSTRAP_START 1
using std::uintptr_t;
volatile uintptr_t __stack_chk_guard=0;
static int stage=0;
static bool valid=true;
struct macho_header {};
namespace dyld3 {
struct MachOLoaded { uintptr_t getSlide() const { return 4096; } };
void kdebug_trace_dyld_marker(int,int,int,int,int) { valid=valid && stage==0; stage=1; }
}
extern "C" __attribute__((no_stack_protector,noinline)) void __guard_setup(const char* apple[]) {
    valid=valid && stage==2 && apple && apple[0] && std::strcmp(apple[0],"stack_guard=fixture")==0;
    __stack_chk_guard=0x12345678; stage=3;
}
extern "C" void sigexc_setup() { valid=valid && stage==3; stage=4; }
extern "C" void _subsystem_init(const char* apple[]) { valid=valid && stage==4 && apple[0]; stage=5; }
namespace dyld {
uintptr_t _main(const macho_header*,uintptr_t slide,int argc,const char* argv[],const char* envp[],const char* apple[],uintptr_t* glue) {
    valid=valid && stage==5 && slide==4096 && argc==1 && std::strcmp(argv[0],"karyo-probe")==0
        && std::strcmp(envp[0],"KARYO_QA=1")==0 && std::strcmp(apple[0],"stack_guard=fixture")==0;
    *glue=0x5678; stage=6; return 0x1234;
}
}
namespace dyldbootstrap {
__attribute__((noinline)) static void rebaseDyld(const dyld3::MachOLoaded*,const char* apple[]) {
    valid=valid && stage==1 && apple[0]; stage=2;
}
"""
    suffix=r"""
}
static const char* arguments[]={"karyo-probe",nullptr,"KARYO_QA=1",nullptr,"stack_guard=fixture",nullptr};
__attribute__((no_stack_protector)) int main() {
    const dyld3::MachOLoaded image{};
    uintptr_t glue=0;
    uintptr_t result=dyldbootstrap::start(&image,1,arguments,&image,&glue);
    if (!valid || stage!=6 || result!=0x1234 || glue!=0x5678 || __stack_chk_guard!=0x12345678) return 1;
    std::puts("{\"bootstrap_returns\":true,\"callbacks_ordered\":true,\"literal_startup_vectors\":true}");
    return 0;
}
"""
    path=args.output/'bootstrap.cpp';path.write_text(prefix+body+suffix)
    binary=args.output/'bootstrap'
    compiled=subprocess.run(['clang++','-std=c++17','-O2','-fstack-protector-all',
        '-mstack-protector-guard=global',str(path),'-o',str(binary)],capture_output=True,text=True,timeout=45)
    (args.output/'compile.log').write_text(compiled.stdout+compiled.stderr)
    compiled.check_returncode()
    run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
    (args.output/'run.log').write_text(run.stdout+run.stderr)
    run.check_returncode()
    report=json.loads(run.stdout)
    if set(report)!={'bootstrap_returns','callbacks_ordered','literal_startup_vectors'} or not all(value is True for value in report.values()):
        raise ValueError('Actual extracted entry did not complete the startup contract')
    report.update(source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),
                  compiled_source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  actual_function_body=True,whole_engine_runtime=False)
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
    return 0


if __name__=='__main__':raise SystemExit(main())
