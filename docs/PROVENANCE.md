# UI foundation provenance

The four theme/paint headers and implementations were reviewed and adapted
from the Karyo desktop prototype at revision
`716d515b765319981b124fb1812a1e3edb9e0ffd`, under
`distro/desktop/shared/libopenkaryon-ui/`.

The public component retains existing color calculations, accent choices,
settings keys, font fallbacks and drawing behavior. Public headers and the
C++ namespace are renamed to `karyo/ui` and `Karyo::Ui`.
The new build boundary exposes an installed CMake target and static development
package, independent of the original monorepository build and OS defaults.
Tests cover the retained behavior and an independent installed consumer.

Copyright (C) 2026 Karyo contributors.
These files and the new build/test code are licensed GPL-3.0-or-later.

Qt is an external dependency and retains its upstream licensing. Font family
names are preferences only. No third-party fonts, icon resources, wallpapers,
private endpoints, machine configuration or private Git history are imported.
## Subsequent reviewed components

The same donor revision supplies desktop entries/icons, window, navigation,
password, menu and tooltip widgets, plus the style/client-frame sources and
headers under distro/desktop/shared/libopenkaryon-ui. The Qt plugin is adapted
from distro/desktop/shared/style-plugin. These are now independent public
CMake components with installed-consumer and regression checks.

Lucide resources are imported separately and retain the license under
resources/lucide/LICENSE. The foundation-only statement above describes
the original slice; desktop integration and the runtime plugin now embed
those attributed icons. No fonts or machine configuration are embedded.

Session keyboard/renderer behavior and labwc configuration are adapted from
distro/distro/packages/openkaryon-shell. The public launcher adds explicit
backend preflight checks, scoped session identity, relocatable configured
installation paths and a separate preparatory package. The complete shell,
PAM lock, optional greeter and privileged system integrations are not included
in this preparation. See SESSION.md for the remaining acceptance gates.

## Public shell and lock

The reviewed shell sources and Russian catalog come from distro/desktop/shell
at the same donor revision. Desktop file operations/type tiles come from
apps/files, and Wi-Fi activation comes from the shared UI library. The optional
catalog is read through XDG data lookup instead of importing App Center data.
The lock and sign-in view come from distro/desktop/signin, excluding greetd.

PAM account failures are rejected; credentials remain outside logs. The public
session uses its own binaries and bus name, excludes private greeter publishing
and compositor rewrite helpers, and retains legacy per-user settings and wire
protocols for compatibility. Optional system integrations remain separate.

Wayland protocol copyright notices remain in the XML files and are installed
with their corresponding runtime package. ext-session-lock-v1 retains Isaac
Freund's MIT notice; foreign-toplevel retains Ilia Bozhinov's notice. The custom
keyboard protocol retains the OpenKaryon Developers MIT attribution.

## Independent applications

Terminal sources and Russian translations are adapted from
`distro/desktop/apps/terminal` at donor revision
`26bad17cef413d26c36c117f2f1123dca4edded7`. They use public Karyo headers/targets,
an independent runtime component and application ID. QTermWidget remains an
external distribution library; no shell profile or SSH configuration is shipped.
Ctrl+click HTTP(S) links, PTY execution, tabs and clipboard behavior are retained.

Camera is adapted from `distro/desktop/apps/camera` at the same application
donor revision. Qt Multimedia remains external. The migrated application keeps
standard user Pictures/Videos locations, explicit recording finalization and
microphone selection; it ships no captured media or device configuration.

Photos comes from `distro/desktop/apps/photos` at the same application donor
revision. The migrated viewer uses atomic QSaveFile publication for rotated
images and standard FileManager1 selection with a default-folder fallback;
it retains the existing per-user Photos/background settings keys.

Calculator expression parsing and laboratory unit/formula primitives, with their
existing regression program, come from `distro/desktop/apps/calculator` at the
same application donor revision. They form a Qt Core-only internal target before
the GUI is migrated. No external scientific SDK or personal data is embedded.

Calculator UI, mode routes, desktop actions and Russian catalog are now adapted
from that same donor component. The Qt Core engine remains an internal library;
GUI packaging and native tests are independent from the shell/session.

Clock sources, Russian catalog and deterministic persisted-timer regression are
adapted from `distro/desktop/apps/clock` at the same application donor revision.
The existing per-user timer file remains compatible. Notifications and optional
alarm playback use available freedesktop/audio services, not a private daemon.

Editor buffers and recovery/history tests are adapted from
`distro/desktop/apps/editor` at the same application donor revision. Saving
unrepresentable Unicode into a loaded Latin-1 file now fails before publication;
explicit UTF-8 conversion retains old/new text. Atomic writes check full length.
The original undo/cache format is retained; the window/UI follows separately.

Editor syntax detection/highlighting and the custom code-editing surface now
use public Karyo widgets. Existing Markdown, CSV/VCF, FASTA and programming
language support is retained without preset-dependent filtering.

Editor window, Russian catalog and launch metadata are now adapted from that
same donor. Existing recovery paths and the session-bus wire contract are kept
to prevent competing instances. The new window validates recovery metadata and
IDs, preserves unknown caches and exposes explicit UTF-8 conversion on save.

PDF Reader sources and Russian translations are adapted from
`distro/desktop/apps/reader` at the same application donor revision. Qt PDF is
an external native distribution dependency. The migrated app preserves user
positions/preferences and its existing session-bus contract; file selection
uses standard FileManager1 rather than a private-monorepository executable.

Media Player sources and Russian catalog are adapted from
`distro/desktop/apps/player` at the same application donor revision. The player
uses native Qt Multimedia, a shared FileManager1 helper and independent runtime
packaging. Natural track sorting uses an explicit non-C collator when the user
locale is C, avoiding Qt's lexical C-locale fallback. No media is packaged.

System Monitor sampling and process grouping are adapted from
`distro/desktop/apps/monitor` at the same application donor revision. The core
reads procfs/sysfs without privilege. Sampling uses a monotonic clock and
read-only fixture roots for deterministic tests; counter resets cannot wrap
into enormous rates and memory keys match complete names. The GUI follows
separately with a review of process/service control and startup overrides.

Monitor pages, launch metadata and Russian catalog are now adapted from the same
donor. The migration observes asynchronous service calls, atomically publishes
user startup overrides and validates process identity for control actions. Native
Linux pidfd syscalls are used for signaling; kernel permissions remain authoritative.

Screenshot annotation/capture primitives and their existing canvas regressions
are adapted from `distro/desktop/apps/snip` at the same application donor
revision. Editable vectors, inline text, raster blur and explicit crop commit
retain one undo history. Clipboard publication now requires successful wl-copy
completion before skipping the Qt fallback, including timeout/error paths.

Snip window/overlay, launch metadata and Russian catalog now come from that
same donor. Existing settings/capture-lock paths are preserved explicitly. The
OpenKaryon window-snapshot v1 wire identity and MIT notice remain in the XML,
with the full MIT text installed alongside it. Its compositor capability is
optional and is never claimed by the stock session merely because the client builds.

The donor's headless wlroots input fixture is retained as test-only source, not
a runtime dependency or installed interposer. It requires an explicitly supplied
QA FIFO and matching wlroots build; real-VM checks use isolated virtual input.

The file-type presentation previously embedded in the shell now has one internal
shared target for shell, Files and Archive. Its existing format classification,
labels and palette behavior are retained, including bioinformatics formats.
Virtual archive entries remain name-only and never sniff filesystem contents.

Archive backend and retained compression/encryption/extraction regressions are
adapted from `distro/desktop/apps/archive` at the same application donor
revision. libarchive and distribution 7-Zip/codecs remain external dependencies.
The two public RAR fixtures retain the full 2013 Konrad Kleine BSD notice and
libarchive v3.7.4 provenance in tests/fixtures/README.md. No personal archive or
password is included; all test passwords are disposable public fixture values.

Archive window, password form, icon and Russian catalog are now adapted from the
same donor. Backend and GUI regressions remain separate executable targets; all
retained UI scenarios are explicitly registered. The optional RAR addon contains
only metadata/documentation and depends on the distribution's own codec package.

Shared clipboard/background file operations are extracted from the already
reviewed shell implementation, matching the donor Files implementation. Copies
now stage complete trees and publish with Linux no-replace rename; errors never
publish incomplete trees or remove move sources. Relative/dangling symlinks and
compound suffixes are retained. Tests operate only in disposable directories.

Foreign-toplevel observation is now an internal shared target, unchanged from
the reviewed shell source. Files can observe application launch completion
without linking the shell or requiring its session to be built. The upstream
protocol XML keeps its original copyright and MIT license notice.

Files folder views, inline rename and details/cache are adapted from
`distro/desktop/apps/files` at the same application donor revision. The
hierarchical size cache remains selection-driven, interruption-aware and bounded;
symlinks are never followed during recursive size accounting. Existing F2
compound-suffix selection and rounded selection visuals retain regression tests.

Files breadcrumb and Places/UDisks2 integration come from the same donor.
Mounting remains an explicit, asynchronous request authorized by the existing
system service. The regression uses an isolated fake system bus and never
registers fake devices on the host or mounts real hardware.
