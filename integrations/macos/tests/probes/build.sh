#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
umask 022
mkdir -p /tmp/karyo-probes/payload/KaryoCocoaProbe.app/Contents/MacOS
cd /tmp/karyo-probes
o64-clang -arch x86_64 -mmacosx-version-min=10.10 -O2 /input/loader.c -o payload/karyo-macos-loader-probe
o64-clang -arch x86_64 -mmacosx-version-min=10.10 -O2 /input/cocoa.m -framework Cocoa -o payload/KaryoCocoaProbe.app/Contents/MacOS/KaryoCocoaProbe
cp /input/cocoa-Info.plist payload/KaryoCocoaProbe.app/Contents/Info.plist
test -f "$(qmake -query QT_INSTALL_LIBS)/libQt5Core.a"
qmake /input/qt.pro
make -j2
cp -R KaryoQtProbe.app payload/
tar -czf /output/probes.tar.gz -C payload .
qmake -query > /output/qmake-query.txt
o64-clang --version > /output/compiler-version.txt
