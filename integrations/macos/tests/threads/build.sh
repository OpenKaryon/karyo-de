#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
umask 022
mkdir -p /tmp/karyo-thread-probe/payload
o64-clang -arch x86_64 -mmacosx-version-min=10.13 -O2 -Wall -Wextra -Werror \
    /input/pthread_mask.c -pthread \
    -o /tmp/karyo-thread-probe/payload/karyo-macos-pthread-mask
tar -czf /output/probes.tar.gz -C /tmp/karyo-thread-probe/payload .
o64-clang --version > /output/compiler-version.txt
