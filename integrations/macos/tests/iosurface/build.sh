#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
umask 022
mkdir -p /tmp/karyo-iosurface/payload
o64-clang -arch x86_64 -mmacosx-version-min=10.13 -O2 /input/buffer.m \
    -framework Foundation -framework IOSurface \
    -o /tmp/karyo-iosurface/payload/karyo-macos-iosurface-probe
o64-clang -arch x86_64 -mmacosx-version-min=10.13 -O2 /input/lifecycle.m \
    -framework Foundation -framework IOSurface \
    -o /tmp/karyo-iosurface/payload/karyo-macos-iosurface-lifecycle
o64-clang -arch x86_64 -mmacosx-version-min=10.13 -O2 /input/layer.m \
    -framework Cocoa -framework QuartzCore -framework IOSurface \
    -o /tmp/karyo-iosurface/payload/karyo-macos-iosurface-layer
tar -czf /output/probes.tar.gz -C /tmp/karyo-iosurface/payload .
o64-clang --version > /output/compiler-version.txt
