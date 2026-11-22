# Optional X11 / Wayland clipboard transport

This ordinary-user adapter preserves UTF-8 text, PNG and lazy local file URLs
between one Wayland compositor and an X11 server. It adapts the prototype
VMware clipboard owner/bridge from donor revision
26bad17cef413d26c36c117f2f1123dca4edded7. Own code is GPL-3.0-or-later;
python-xlib, X11, XFixes, wl-clipboard and xclip retain their upstream licenses.

Build the standalone integrations/clipboard CMake entry and install the
xclipboard-runtime component. It installs no service/autostart or VMware daemon,
changes no system configuration and does not claim the host clipboard by itself.
An ordinary graphical session may explicitly run karyo-xclipboard with both
DISPLAY and WAYLAND_DISPLAY set. Stop it before another adapter owns that pair.

The X11 owner provides TARGETS, TIMESTAMP, MULTIPLE and bounded incremental
transfers, leaving PRIMARY unchanged. Local file lists are copied as URLs,
without opening VMware vmblock staging paths. File-copy semantics are preserved;
cut is deliberately normalized to copy. Supported limits are 1MiB for text/file
URLs and 32MiB for PNG. Clipboard bytes are not written to disk or logged.

This is one protocol component of VMware integration. Active-seat vmusr handover,
actual VMware host exchange, drag/drop, shared folders and display following
remain separate integration work. Family sharing is a different opt-in package.

Tests use an isolated Xvfb and headless labwc as an ordinary uid. Source protocol
acceptance is not evidence that VMware's host channel or installed native guest
integration has passed. See Builder's validation records for actual results.

The standalone ordinary-user suite currently passes all21 cases on Debian13 and
Ubuntu26.04. Its Xvfb owner tests cover MULTIPLE, repeated multi-megabyte INCR,
bounded simultaneous requests and oversized-property refusal. A real isolated
headless labwc plus independent Xvfb pair verifies identical-image recopy after
PRIMARY changes, PNG and lazy file URLs in both directions, and local sensitive
preservation during a remote copy. This does not use the VMware host clipboard.

The owner limits INCR streams to32 and reads MULTIPLE metadata with a fixed
bound; the bridge retains at most4 owner processes. SIGTERM/SIGHUP/SIGINT run
owned-process cleanup, X11 connections close and notifier drains have time/byte
bounds. Sensitive password-manager hints are neither exported nor replaced by
an incoming X11 value. These are metadata hints, not secret-content detection.

Protocol reference: [ICCCM selections](https://www.x.org/releases/X11R7.7/doc/xorg-docs/icccm/icccm.html).
No upstream text/code from that specification is embedded here.

The fixed-host tests/installed-acceptance.py gate now also passes all21 cases
against the actual installed modules on both disposable native guests, with
capabilities dropped and NoNewPrivileges for the ordinary QA account. It checks
privileged/missing-display invocation refusal, unchanged original Home,
account/PAM/display-manager hashes and clean package verification. Captured
QA test files are removed; root0600 receipts retain only test outcomes/metadata.
The complete DE build and all70 CTests pass on each base as well. Those native
protocol tests use their own Xvfb/headless compositor, leaving the original
Home display and VMware host clipboard untouched.
