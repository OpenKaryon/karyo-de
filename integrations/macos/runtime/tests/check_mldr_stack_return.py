#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real mldr termination body returning from a separate live stack."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.source.read_text()
    declaration = re.search(r'extern void __mldr_return_to_linux_stack\(jmp_buf, int\)\s+__asm__\("longjmp"\) __attribute__\(\(noreturn\)\);', source)
    if declaration is None:
        raise ValueError('Missing the native cross-stack return declaration')
    start = source.index('int __darling_thread_terminate(')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[start:end]
    args.output.mkdir(parents=True, exist_ok=False)
    fixture = r"""#define _GNU_SOURCE
#include <pthread.h>
#include <setjmp.h>
#include <stdbool.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
DECLARATION
static __thread jmp_buf t_jmpbuf;
static __thread void *t_freeaddr;
static __thread size_t t_freesize;
static __thread int t_server_socket = -1;
static __thread void *darwin_stack;
static atomic_uint completed;
static int dserver_rpc_explicit_checkout(int fd,int port,int flag) { return 0; }
static int dserver_rpc_checkout(int port,int flag) { return 0; }
static void dserver_rpc_explicit_kprintf(int fd,const char *text,size_t size) { abort(); }
static void dserver_rpc_kprintf(const char *text,size_t size) { abort(); }
static void __mldr_close_rpc_socket(int fd) { }
BODY
static void leave_darwin_stack(void) {
    __darling_thread_terminate(darwin_stack, 1024 * 1024, 0);
    abort();
}
static void *worker(void *arg) {
    darwin_stack = mmap(NULL,1024 * 1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(darwin_stack == MAP_FAILED) abort();
    t_server_socket = (uintptr_t)arg & 1 ? 123 : -1;
    if(setjmp(t_jmpbuf)) {
        if(t_freeaddr!=darwin_stack || t_freesize!=1024 * 1024) abort();
        if(munmap(t_freeaddr,t_freesize)!=0) abort();
        atomic_fetch_add(&completed,1);
        return NULL;
    }
    uintptr_t top = ((uintptr_t)darwin_stack + 1024 * 1024 - 8) & ~(uintptr_t)15;
    top -= 8;
    *(uintptr_t *)top = 0;
    __asm__ volatile("movq %0, %%rsp; xorq %%rbp, %%rbp; jmp *%1"
        : : "r"(top), "r"(leave_darwin_stack) : "memory");
    __builtin_unreachable();
}
int main(void) {
    for(uintptr_t i=0;i<16;++i) {
        pthread_t thread;
        if(pthread_create(&thread,NULL,worker,(void *)i)!=0) return 1;
        if(pthread_join(thread,NULL)!=0) return 2;
    }
    if(atomic_load(&completed)!=16) return 3;
    puts("{\"passed\":true,\"normal_stack_returns\":16}");
    return 0;
}
""".replace('DECLARATION', declaration.group()).replace('BODY', body)
    driver = args.output / 'stack-return.c'
    driver.write_text(fixture)
    binary = args.output / 'stack-return'
    commands = [('compile', ['clang', '-std=gnu11', '-O2', '-D_FORTIFY_SOURCE=2', '-fstack-protector-strong', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(driver), '-pthread', '-o', str(binary)]), ('run', [str(binary)])]
    for name, command in commands:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        (args.output / (name + '.log')).write_text(result.stdout + result.stderr)
        result.check_returncode()
    report = json.loads(result.stdout)
    if report != {'passed': True, 'normal_stack_returns': 16}:
        raise ValueError('The normal live-stack returns did not complete')
    report.update(source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(), actual_macho_runtime=False, kind='native-mldr-function-body')
    (args.output / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    print(json.dumps(report))


if __name__ == '__main__':
    main()
