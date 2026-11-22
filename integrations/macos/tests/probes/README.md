# Real Intel macOS application probes

These GPL-3.0-or-later sources compile to real Intel64 Mach-O applications.
They are not Linux engine fixtures and are never installed in the DE package.
The console program exercises libSystem startup, temporary UTF-8 file I/O and
JSON-encoded literal argv; --exit17 permits an actual exit-code propagation
check. The Cocoa bundle exercises an ordinary window, UTF-8 label, target/action
button and bounded event loop. The Qt Widgets bundle exercises a visible window,
button signal, UTF-8 label, timer and widget raster output.

Builder's tests/acceptance/build-macos-probes.py uses a separately provided local
Linux amd64 cross SDK, resolves its immutable image ID and runs without network,
capabilities or host write access except a fresh output directory. It requires
an Intel macOS Clang wrapper and static Qt5 cross build. The first SDK contained
a Mac-target macdeployqt; static applications need no deployment invocation.
No SDK binary or Qt/Forge implementation is copied into this repository.

The output is a Linux-created tar preserving executable modes and bundle links,
not a Windows filesystem extraction. The Builder driver checks all three actual
Mach-O headers, member paths/modes/links and hashes. Its receipt distinguishes
source revision, dirty source bytes and external SDK identity. Compiled probes
are not runtime evidence: runtime_tested remains false until the installed
Darling engine actually executes them on each native kernel/desktop. A Cocoa or
Qt self-report is supplemented by separate visible-window evidence during that
runtime acceptance; compilation alone establishes no GUI compatibility.
