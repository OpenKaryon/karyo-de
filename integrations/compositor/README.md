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

Current compositor recipes require the added scene-mask declaration in the native
wlroots development package. First build the matching wlroots recipe described
below and put its matching runtime and development DEBs into a disposable SDK
image based on the native compositor builder. Install both there with dpkg -i
and require apt-get check before using that image for the compositor command.
Use Debian wlroots karyo4 or Ubuntu karyo3; stock headers cannot compile this API.
These are SDK preparation steps inside a container, not host package commands.
The --image arguments below refer to that prepared native image, whose immutable
identity is captured by Builder. Name it karyo-compositor-sdk:trixie (or
karyo-compositor-sdk:ubuntu2604). No private temporary image ID is required.


From a Karyo DE checkout with Karyo Builder installed:

    docker build -t karyo-source-builder:trixie ../karyo-builder/environments/source-debian13
    docker build -t karyo-compositor-builder:trixie integrations/compositor/environments/debian
    python integrations/compositor/fetch-inputs.py --base debian --image karyo-compositor-builder:trixie
    karyo-builder source-package integrations/compositor/recipes/debian.json --image karyo-compositor-sdk:trixie --output out/compositor/debian/final-packages

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
to its complete distribution version. The later startup-lock extension uses +karyo2. It does not replace a display manager,
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

## Protected-session startup lock

The optional --startup-lock command-line flag sets the existing abandoned-lock
state after server initialization but before backend/output startup. Output
creation immediately adds opaque lock backgrounds, normal input stays subject
to the native locked-session policy, and the first ext-session-lock client
adopts the lock. Its failure/disconnect leaves the compositor locked; an explicit
protocol unlock is required. Unflagged startup keeps upstream behavior.

The derivative src/main.c patches retain GPL-2.0-only. Ubuntu explicitly includes
session-lock.h because its newer labwc.h intentionally leaves the manager opaque.
No user/program-specific window rule, new display manager or library ABI is used.
Both native +karyo2 packages build with upstream tests and pass ordinary UID 65534
protocol acceptance in read-only, networkless containers. In addition to the
ordinary protocol scenario, tests/native-acceptance.py --startup-lock starts the
actual packaged compositor before any lock client. A normal mapped window stays
unavailable; a first real locker adopts the state, disconnects without unlock,
and a replacement locker must explicitly unlock before that window is visible
to snapshots. Original layout, minimize, sandbox and lock/unlock checks then run.

This closes the headless native state gate only. Installed initial-session PAM,
physical input/visual output, VT readiness and privileged profile switching have
separate acceptance. Protocol snapshot denial alone is not a pixel capture or
proof of physical keyboard behavior. DE session wiring must fail closed when a
protected startup lacks this compositor capability rather than silently starting
an unprotected stock compositor.


## Generic Win32 hidden-owner groups

The karyo3 source recipes add a separate reviewed owned-window patch. Legacy
Win32 toolkits can place one decorated main frame and fixed-size palettes under
an invisible application owner. The compositor selects that unique frame as an
effective stacking root only for matching positive PID and case-insensitive .exe
class, a tiny undecorated owner, and an unambiguous document layout. It preserves
the actual WM_TRANSIENT_FOR hints. Native classes, visible owners, multiple main
frames and resizable custom frames retain the normal compositor path.

Minimization of this recognized group disables its scene views without sending
IconicState, which can otherwise make the application withdraw its real windows
and disappear from the taskbar. Debian 0.8's raise-cache bypass also handles
palettes that appear later. Ubuntu 0.9 has no such cache and uses the native
wlroots0.19 window-type API; no Debian library is bundled or forced onto Ubuntu.

The patches adapt the prototype's wine-owned-window-groups and minimize changes
at donor26bad17cef413d26c36c117f2f1123dca4edded7. They modify GPL-2.0-only labwc
and retain its upstream licensing; the standalone QA driver is GPL-3.0-or-later.
There are no application-name rules, prefix mutations or per-program settings.

Run tests/owned-windows.py with --package pointing to a verified native source-
package result and --output to a new directory, as an ordinary user in the
matching prepared image. It owns a private headless labwc/XWayland session and
uses captured pixels, X11 hints and state for five layouts. It also checks four
late palettes, unrelated-app occlusion and grouped minimize/restore. --baseline
requires the old missing-group behavior and is deliberately not acceptance.
Use a read-only source/root, isolated /tmp, no network, cap-drop=ALL and
no-new-privileges. The test waits for compositor-published X11 retirement before
XIDs may be reused between cases; XSync alone does not acknowledge its WM loop.

Both karyo2 packages reproduce the missing Wine grouping. Both karyo3 packages
pass the new native scenario and the existing normal/startup-lock protocol
gates. Native package builds retain upstream test execution (two Debian suites,
three Ubuntu suites). This proves the generic XWayland layout model, not actual
Wine application compatibility, focus-loop/menu fixes, caption themes or a live
package upgrade. Those remain distinct migration gates. The running QA desktop
compositors and user virtual machines were not replaced for these tests.


### Debian group raise cache correction

The Debian karyo4 recipe restores the native last-raised-view cache instead of
unconditionally bypassing it for an effective Win32 main frame. A newly mapped
view invalidates the cache before its first normal raise, so late palettes still
join the group. Repeated activation of an already raised main frame does not
restack all its siblings again or create extra X11 focus traffic. No application
name, Wine registry option, client ownership rewrite or new public ABI is used.
The independent GPL-2.0-only patch follows labwc's existing source license.

The final native source package passes the five-layout X11 model (including four
late palettes), ordinary protocol acceptance and fail-closed startup-lock checks.
Actual Wine also exposed separate late-buffer association and FocusIn feedback
bugs in native wlroots; fixing the raise cache alone is not full Wine acceptance.


## Native wlroots Xwayland lifecycle corrections

The optional native wlroots recipes keep Debian's 0.18.2-3 and Ubuntu's
0.19.2-1 source/SONAME boundaries. They do not move Ubuntu libraries to Debian,
replace an existing user's compositor or automatically activate system packages.
Use the matching `environments/debian-wlroots` or `environments/ubuntu-wlroots`
SDK, fetch with `--base debian|ubuntu --component wlroots`, and build the matching
`recipes/debian-wlroots.json` or `recipes/ubuntu-wlroots.json` with Builder's
`source-package` command. Actual package builds have no network.

X11 serials and Wayland buffer commits arrive over different connections. The
native role previously connected its commit listener at association but ignored
a buffer already committed before that point. Both source recipes apply the
existing role-owned commit handler after `associate`, once the compositor's map
listener is connected. A null-buffer surface stays unmapped; an existing buffer
uses exactly the normal map rule. No role operation is forced from labwc.

Debian additionally separates acknowledging a legitimate same-process FocusIn
from requesting focus again. Stale notifications are ignored; accepted positive
PID handoffs update X11 focus bookkeeping without another WM_TAKE_FOCUS. A real
foreign-process focus request still takes the existing denial path. This adapts
the native 0.19 focus-state approach without new functions, fields or symbols in
0.18's public API. It is independent of executable names or Wine settings.

Ubuntu's strict native rebuild also ignores new libinput switch types which its
0.19 ABI cannot represent, instead of treating them as a laptop lid, and keeps
the cursor path's strchr result const-correct with C23 headers. Compiler Werror,
native packaging options and dependency checks remain enabled. All derivative
wlroots patches retain the original MIT license in patches/WLROOTS-LICENSE;
original source-package copyright data is preserved.

The native recipes execute dh_auto_test, but these upstream wlroots releases
report `No tests defined`. Validation therefore uses actual installed Wine
fixtures and separate compositor protocol/model gates. Exported dynamic symbols
and SONAME match stock native libraries exactly: 769 defined symbols on Debian
0.18 and 823 on Ubuntu 0.19. The combined native runtime passes all five X11
layout cases, four late palettes and ordinary/startup-lock protocol acceptance
on both bases. Physical input-switch behavior and GPU/hardware output are not
claimed by these headless checks.


## Fully unmapped Win32 framework owners

A raw X11 owner may never create an associated Wayland surface. Its properties
can also predate the XWM's PropertyChange subscription. A missing in-memory
WM_CLASS therefore did not prove that the client had no class: an explicitly
different owner class could have been missed. The native wlroots karyo2 recipes
refresh WM_CLASS and Motif decoration hints when a transient references an
unassociated parent. The XWM batches two bounded property reads at that point,
not during rendering or repeated stacking. Client hints themselves are unchanged.
The change uses the existing parsers and private functions; public structures,
exported symbols and SONAME stay unchanged on each native ABI.

The corresponding labwc patch may borrow the visible sibling's class only for
a tiny, undecorated, unassociated owner with no class and a matching positive
PID. The unique main-frame and fixed-palette guards remain. The runtime package
requires the exact native wlroots karyo2 version that performs this metadata
refresh; a stock library must not be silently treated as having read the hints.
Updating that library requires rebuilding and validating the matching pair.
The ordinary mapped-owner path remains covered separately.

Both mapped and fully unmapped owners pass actual PE32/PE64 palette, minimize,
restore and focus cases on Debian and Ubuntu. The expanded ten-layout X11 model
also rejects a truly different owner class, a foreign-process owner, native
classes, an untyped mapped owner and ambiguous documents. Normal and startup-lock
protocol gates pass against both installed pairs. This extends the previous
mapped-owner checkpoint; it does not establish arbitrary toolkit compatibility,
Windows-like decoration or existing-user deployment.


### Absolute pointer re-entry on the Debian compositor

Debian labwc karyo6 follows native labwc0.9.3 behavior: absolute position updates
no longer emit an additional relative-motion event to the focused client. The
relative-device handler remains unchanged. This avoids an Xwayland cursor jump
to the output edge when leaving/re-entering a Wine surface and clicking.
It is a generic input transport correction, without application-name rules.

The same real Wine2 producer/actor reproduces the old failure and passes with
the new compositor; actual X11 position, button mask and focus are retained.
Both PE32/PE64 and96/192DPI pass all15 baseline stages after the correction.
The native ten-layout owned-window model and normal/startup-lock protocol gates
also pass, as do real Wine palette/focus checks. Native wlroots karyo2 and its
existing exact dependency remain unchanged. This does not claim physical tablet,
locked-pointer gameplay or full remaining DE/runtime migration acceptance.


## Native scene masks

The native wlroots karyo3 recipes add wlr_scene_node_set_clip_region, which copies
a node-local Pixman region through the existing addon lifecycle. Rendering,
opaque occlusion, hit testing and damage follow masks on the node and its
ancestors. Passing NULL removes the mask. Destroying a subtree releases its
private masks and preserves the lifecycle of independent addons and buffers.
No public structure field, old export or native SONAME is changed.

Both native source builds pass an unchanged old-header executable against the
new library, including output pixels, native input queries, copied/complex and
nested regions, moving/reparenting, empty/cleared masks and destruction. Tests
cover integer scales 1/2 and normal/90-degree output transforms. A headless native
scanout path accepts an opaque full-output buffer, rejects it after clipping
its corners and accepts it again after the mask is removed. Physical DRM and
fractional scale remain separate gates. The synthetic buffer explicitly chooses
nearest filtering to keep its solid-color reference exact; desktop filtering is
unchanged.

Debian labwc karyo7 and Ubuntu labwc karyo5 require the exact native wlroots
karyo3 version. Both pairs pass mapped/unmapped real Wine palettes, the ten-layout
X11 model and ordinary/startup-lock protocols. Labwc's native test suites also
pass; upstream wlroots reports no defined tests, so the own runtime fixtures
provide the new feature gate. Builder docs/VALIDATION-SCENE-CLIP.md records the
exact packages, source and acceptance identities.

That API checkpoint supplied the renderer/input mechanism without applying it
to application frames. The rounded-frame integration below now uses it with
per-view themes and actual pointer tests. Registered app icons and broader real
application/deployment coverage remain separate integration work.


## Per-view Windows theme foundation

The native labwc recipes now add win32-window-theme.patch: Debian +karyo8 and
Ubuntu +karyo6. XWayland views whose actual class ends in .exe (case insensitive)
select an independent built-in Karyo-Windows frame. Other XWayland and Wayland
clients keep the configured Linux theme. A late class change rebuilds an existing
SSD, and reconfiguration recreates both themes before native view reload.

This foundation carries a 23-logical-pixel light caption, compact font and
separate button hitboxes. Ubuntu uses its native gradient/pattern API and split
SSD-button module; it does not receive Debian source or a downgraded library.
Windows hover generation avoids native callbacks tied to the global Linux theme.
Existing Linux callbacks and decoration behavior remain native.

Run tests/owned-windows.py in the same isolated native runtime described above.
Before its ten grouping cases, it renders native and .EXE clients simultaneously
against custom Linux colors/button sizes. It measures actual caption pixel rows,
checks active/inactive colors, changes an already mapped class to Wine and back,
and reloads both themes while clients stay mapped. The old packages fail the
new Wine-color requirement. This is an X11 model test, separately accompanied by
real PE32/PE64 Wine palette regressions in Builder acceptance.

This is an intentionally separate prerequisite, not the completed Windows theme.
Caption assets/widths and rounded client/input corners are covered below.
Application icon matching and real third-party applications remain separate
gates. Wine-internal child/MDI shaping and scene-mask API acceptance are
independently documented.


## Windows caption assets and hitboxes

Debian +karyo9 and Ubuntu +karyo7 add win32-caption-buttons.patch. Caption controls
use 46 logical pixels, while the app-icon button uses 24. Visible-button fitting,
initial layout, resized layout, title offsets, icon size and actual hitboxes all
use the same per-button width. Narrow windows keep native hide-priority behavior
without treating the icon as a full-width control. Native Linux widths stay
configurable and unchanged.

The optional karyo-win32 runtime component installs five original GPL-3 SVGs in
share/themes/Karyo-Windows/labwc: close, close_hover, iconify, max and max_toggled.
They provide thin Windows-style glyphs and a filled red close-hover background
with a white cross. Asset lookup uses this independent theme instead of the
current Linux icon resources. No third-party application or per-program rule
is involved. Without that optional component native bitmap fallbacks remain.

The isolated caption-buttons.py producer uses a private virtual pointer and an
actual X11 client. It checks red/white hover pixels at the new left hitbox edge,
clicks maximize/restore/minimize and normal WM_DELETE_WINDOW close, then checks
narrow widths. --baseline separately observes the previous missing buttons;
it does not satisfy positive acceptance. SVG thin-stroke antialiasing and the
software cursor are accounted for without accepting the light title background
as white glyph ink. Rounded frame clipping is documented below; registered app icons remain a
subsequent step.


## Floating Windows frame corners

Debian labwc +karyo11 requires wlroots 0.18 +karyo4; Ubuntu labwc +karyo8 retains
native wlroots 0.19 +karyo3. Ordinary floating Wine SSD frames use a six-logical-pixel
outer radius at both ends, including curved lower border buffers. Content matches
the inner border. A titlebar ancestor mask also covers button hitboxes, leaving
shadows and native resize extents outside that mask. Fully maximized/fullscreen
frames remain square. Rounded image variants remain populated; Windows variants
use the shared scene mask rather than global Linux image-modifier geometry.

Opaque managed Wine dialogs/utilities with an actual parent and client-painted
captions use a four-pixel content mask. Normal custom CSD windows, ARGB surfaces,
menus, tooltips and override-redirect popups keep their own shape. Parent/type/
class changes recompute or clear the mask, as do geometry and decoration changes.
Reload/unmap destroys or refreshes masks through the existing scene lifetime.
There is no program-name rule or Wine-prefix override.

Debian's old XWM ignored property deletion for WM_TRANSIENT_FOR and window type.
The native 0.19.2 parser semantics are backported to clear stale metadata and
emit existing notifications, including safe empty type arrays. All 114 public
headers, 770 existing exports and SONAME match the accepted wlroots3 ABI exactly.
Ubuntu already has this behavior; its library is not rebuilt for this step.

Builder check-caption-buttons.py --scenario rounded-frames freezes the actor and
shared harness, verifies native artifacts and runs 23 real X11 pointer/pixel
phases per base. They cover all four corners, click-through on owned cutouts,
normal client input, caption corner safety, resize, minimize/restore, max/fullscreen,
reload, removed/restored parent/type/class, normal CSD, ARGB and override popups.
--baseline observes the previous square package separately. The default caption
scenario still checks visible control glyphs and real edge actions.

check-win32-palettes.py --rounded-frames additionally checks the four corners of
the actual PE32/64 main and four owned tools after group restore, with mapped and
unmapped tiny owners. Real menu, child-glyph, native model/protocol and lock gates
remain separate. These are headless/Pixman scale 1 tests; physical GPU, fractional
scaling, app icon matching, third-party programs and full current-HEAD deployment
are not established by this checkpoint.
