#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
umask 022
mkdir -p /tmp/karyo-properties/payload
cd /tmp/karyo-properties

o64-clang -arch x86_64 -mmacosx-version-min=10.13 -O2 /input/window-properties.m \
    -framework Cocoa -o payload/karyo-macos-window-properties-probe
o64-clang -arch x86_64 -mmacosx-version-min=10.13 -O2 /input/menu-view-ownership.m \
    -framework Cocoa -o payload/karyo-macos-menu-view-probe

tar -czf /output/probes.tar.gz -C payload .
o64-clang --version > /output/compiler-version.txt
