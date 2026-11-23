#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check normal numerical results and scalar calls from the actual libm sources."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    cmake=(args.source/'CMakeLists.txt').read_text()
    line=next(row for row in cmake.splitlines() if row.startswith('set(CMAKE_C_FLAGS '))
    flags=re.search(r'set\(CMAKE_C_FLAGS\s+"([^"]+)"',line)
    if flags is None or '-fno-builtin' not in flags.group(1).split():raise ValueError('The actual math-library flags must keep scalar calls')
    args.output.mkdir(parents=True,exist_ok=False)
    header=(args.source/'Source/math.h').read_text()
    structures=[]
    for kind in ('float','double'):
        match=re.search(r'struct __'+kind+r'2 \{[^}]+\};',header)
        if match is None:raise ValueError('Missing actual ABI return structure')
        structures.append(match.group())
    stub='\n'.join(structures)+r"""
#define M_PI 3.14159265358979323846264338327950288
double sin(double); double cos(double); float sinf(float); float cosf(float);
double pow(double,double); float powf(float,float);
struct __float2 __sincosf_stret(float); struct __double2 __sincos_stret(double);
struct __float2 __sincospif_stret(float); struct __double2 __sincospi_stret(double);
double __exp10(double); float __exp10f(float);
"""
    (args.output/'math.h').write_text(stub)
    files=[]
    for name in ('sincos.c','exp10.c'):
        p=args.output/name;p.write_bytes((args.source/'Source'/name).read_bytes());files.append(p)
    cases=[]
    for value in (0,.125,.5,1,2,3):
        cases.append('{'+','.join(format(x,'.17g') for x in (value,math.sin(value),math.cos(value),math.sin(value*math.pi),math.cos(value*math.pi)))+'}')
    harness=r"""#include <stdio.h>
#include "math.h"
struct Values { double x,s,c,sp,cp; };
static const struct Values values[]={CASES};
static int near(double a,double b,double tolerance) { double d=a-b; if(d<0)d=-d; return d<=tolerance; }
int main(void) {
    unsigned checked=0;
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) {
        const struct Values v=values[i];
        struct __double2 d=__sincos_stret(v.x),dp=__sincospi_stret(v.x);
        struct __float2 f=__sincosf_stret((float)v.x),fp=__sincospif_stret((float)v.x);
        if(!near(d.__sinval,v.s,1e-12)||!near(d.__cosval,v.c,1e-12)||!near(dp.__sinval,v.sp,1e-12)||!near(dp.__cosval,v.cp,1e-12))return 1;
        if(!near(f.__sinval,v.s,5e-6)||!near(f.__cosval,v.c,5e-6)||!near(fp.__sinval,v.sp,5e-6)||!near(fp.__cosval,v.cp,5e-6))return 2;
        checked+=4;
    }
    double expected=1;
    for(unsigned i=0;i<3;++i) { if(!near(__exp10(i),expected,1e-12)||!near(__exp10f(i),expected,5e-6))return 3; expected*=10;checked+=2; }
    printf("{\"passed\":true,\"functions_checked\":6,\"normal_calls\":%u}\n",checked);
    return 0;
}
""".replace('CASES',','.join(cases))
    driver=args.output/'driver.c';driver.write_text(harness)
    binary=args.output/'math-calls'
    def run(label,argv):
        p=subprocess.run(argv,capture_output=True,text=True,timeout=40)
        (args.output/(label+'.log')).write_text(p.stdout+p.stderr)
        p.check_returncode();return p.stdout
    run('compile',['clang','-std=c11','-O3','-fno-builtin',*(str(p) for p in files),str(driver),'-lm','-o',str(binary)])
    result=json.loads(run('numeric',[str(binary)]));assert result['passed'] is True and result['normal_calls']==30
    emitted={}
    for file in files:
        ir=args.output/(file.stem+'.ll')
        run(file.stem+'-darwin',['clang','-target','x86_64-apple-darwin20','-O3','-fno-builtin','-S','-emit-llvm',str(file),'-o',str(ir)])
        emitted[file.stem]=ir.read_text()
    for name,source,callees in [('__sincos_stret','sincos',('sin','cos')),('__sincosf_stret','sincos',('sinf','cosf')),('__exp10','exp10',('pow',)),('__exp10f','exp10',('pow',))]:
        match=re.search(r'^define[^\n]*@'+name+r'\([^\n]*\{\n(.*?)^}',emitted[source],re.M|re.S)
        if match is None or not all('@'+callee+'(' in match.group() for callee in callees):raise ValueError('Missing normal scalar implementation calls: '+name)
        if re.search(r'call[^\n]*@'+name+r'\(',match.group()):raise ValueError('The helper did not retain its scalar implementation')
    result.update(darwin_scalar_calls=True,actual_source_files={name:hashlib.sha256((args.source/'Source'/name).read_bytes()).hexdigest() for name in ('sincos.c','exp10.c')},cmake_sha256=hashlib.sha256((args.source/'CMakeLists.txt').read_bytes()).hexdigest(),whole_engine_runtime=False)
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    return 0


if __name__=='__main__':raise SystemExit(main())
