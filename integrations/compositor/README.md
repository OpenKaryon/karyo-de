# Optional native compositor protocols

Karyo's keyboard indicator and screenshot window picker use two private Wayland
extensions. Stock labwc does not provide them. This integration rebuilds each
distribution's native source with reviewed patches rather than shipping an old
Debian compositor/library stack on Ubuntu.

| Base | Native source | Native wlroots |
| --- | --- | --- |
| Debian 13 / trixie | labwc 0.8.3-1 | libwlroots-0.18 |
| Ubuntu 26.04 / resolute | labwc 0.9.3-1 | libwlroots-0.19 |

Patches and portable checksum-locked recipes belong to DE. Generic source capture,
quilt application and native packaging belong to Karyo Builder's source-package
stage. Sources/downloads/build results stay in ignored out/compositor. No upstream
repository, personal configuration or downloaded binary is committed.

## Prepare and build

From a Karyo DE checkout with Karyo Builder installed:

    docker build -t karyo-source-builder:trixie ../karyo-builder/environments/source-debian13
    docker build -t karyo-compositor-builder:trixie integrations/compositor/environments/debian
    python integrations/compositor/fetch-inputs.py --base debian --image karyo-compositor-builder:trixie
    karyo-builder source-package integrations/compositor/recipes/debian.json --image karyo-compositor-builder:trixie --output out/compositor/debian/final-packages

Ubuntu uses source-ubuntu2604, environments/ubuntu, recipes/ubuntu.json and image
tags karyo-source-builder:ubuntu2604 / karyo-compositor-builder:ubuntu2604.
Pass --base ubuntu to the downloader and use a separate output directory.

Dockerfile dependency preparation and source downloading use signed native APT
indexes inside disposable containers. The downloader checks every file against
the reviewed recipe and preserves any differing existing input for inspection.
It resolves an existing local image ID and does not install host dependencies.
The actual source-package build has no network and refuses a different base.

The native quilt series is preserved. Our ordered additions enable physical
layout control, then scene snapshots, then upstream test execution and the MIT
XML copyright notice in the native package. Upstream tests run with Meson's test
feature enabled: two suites on 0.8.3 and three on 0.9.3. No nocheck is requested.
Existing runtime dependencies, sessions, icons, hardening flags and upstream
copyright data remain native. The package keeps the name labwc and adds +karyo1
to its complete distribution version. It does not replace a display manager,
activate Karyo or install a wlroots library.

## Protocol behavior and adaptation

The openkaryon_keyboard_layout_v1 and openkaryon_window_snapshot_v1 wire names
remain compatible with the migrated clients. They identify protocols, not public
product names. Both globals are privileged in labwc's security-context filter;
sandbox clients cannot discover or bind them. Requests reveal no window data
and change no group while the session is locked.

Layout requests use the physical keyboard group, ignore virtual keyboards and
handle missing keyboard/keymap state. They preserve native modifier broadcasting
and per-window group restoration. Snapshots traverse enabled scene nodes in
actual front-to-back stacking order, include mapped non-minimized toplevels and
decoration extents, and finish with a separate availability flag. Disabled
workspaces are excluded by the scene tree. This exposes visible-screen capture
bounds; occluding windows remain in the captured pixels.

Ubuntu's node descriptors store the view separately and use LAB_NODE_VIEW;
its title is a native view field. Those adaptations are explicit in the Ubuntu
patch, rather than forced through fuzz or a downgraded library. Both variants
include the definitions they use for session-lock and scene/seat state.

## Native acceptance

Run tests/native-acceptance.py inside the matching prepared image as an ordinary
user with the DE checkout mounted read-only at /source, an output mount at
/output and a writable executable /tmp. Pass the published source-package
directory as --package and /output as --output. Use the immutable image ID
recorded by that build, network=none, read-only root, UID 65534, cap-drop=ALL and
no-new-privileges. The script verifies artifact hashes, extracts the native DEB,
compiles real Wayland clients and runs the packaged compositor with headless
outputs and pixman. It preserves compositor/probe logs and a compiled probe.

The real client creates two SHM-backed xdg toplevels and verifies their order,
minimizes one, connects another client through the actual security-context
protocol, and tests snapshot/layout behavior during a real session lock and
after unlock. It does not substitute a fake compositor.

The generated protocol-probe --keyboard additionally checks actual keyboard
modifier events and group cycling in a disposable graphical VM with us,ru
configured. It briefly locks that display, so use only an isolated QA session.
Headless acceptance establishes safe no-physical-keyboard behavior; it does not
claim to test a physical group.

Both final packages passed headless UID-65534 acceptance and installed VMware
sessions. Real installed shell clicks changed RU to EN; installed Snipping Tool
window selection captured the terminal including its titlebar and opened the
editor. Native ABI libraries remained unchanged. Stock package rollback and
reinstallation passed, and original keyboard configuration and LightDM were
restored after the checks.

| Base | Accepted version | labwc DEB SHA256 |
| --- | --- | --- |
| debian | 0.8.3-1+karyo1 | 9a62cdbb3401dc344edd43793d7c303740219c94526a410a8ff8764e5353a21e |
| ubuntu | 0.9.3-1+karyo1 | d3ae0e6a94add680dc6778f451593a0542a99051a030dd6f28212aaf25f17551 |

This closes the layout/snapshot extension gate only. Wine/VMware decoration and
owned-window patches, profile/account/PAM/network/DNS/shared-clipboard helpers,
physical GPU acceptance and Files background-window activation remain separate
migration work. No package is published by this integration.

## Provenance and licensing

Reviewed donor inputs are distro/distro/patched/labwc/keyboard-layout-control.patch
and window-snapshot.patch at prototype revision
26bad17cef413d26c36c117f2f1123dca4edded7. Native source provenance and checksums are
recorded separately in each recipe; actual outputs record immutable build image,
native package inventory, runner hashes, patch hashes and full native records.

The patched upstream C/header files retain labwc's GPL-2.0-only licensing and
notices; the derivative patches follow those existing file licenses. Protocol
XML retains OpenKaryon Developers' MIT notice and full MIT terms in native
copyright metadata. New independent Python tooling and the standalone acceptance
client are GPL-3.0-or-later. These distinctions preserve the third-party licensing
boundary instead of labeling the upstream compositor as GPL-3.
