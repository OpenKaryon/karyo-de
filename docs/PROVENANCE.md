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

Files quick archive actions retain their public behavior (single-root flattening,
compound suffixes, ZIP/TAR.GZ/GZIP and streamed decompression) but now use the
shared Archive backend for creation/extraction. Worker failures are always
asynchronous and visible; complete results publish with no-replace rename.
Unsafe paths, missing engines, protected archives and failed output publication
cannot leave partial results or erase pre-existing outputs.

Files window/service and retained IPC/archive-handoff regressions come from the
same donor. The private `org.openkaryon.Files` wire contract and explicit legacy
user settings remain compatible; public executable/desktop/translation names
use Karyo. FileManager1 is claimed by the optional Files server only in a running
Karyo session, without installing a generic activation override for other DEs.

Settings configuration starts from the application donor's config.cpp/h.
Desktop/shell preference locations remain compatible. Session configuration
now follows the public launcher's path precedence, preserves unknown legacy
lines on the first public edit and atomically writes safely quoted literals.
It never executes a profile while reading it and reports validation/write errors.

Settings row/toggle/segment/fact-card/page widgets are adapted from the same
application donor. The local command adapter now delivers exactly one bounded
asynchronous success/failure, limits buffered output and cancels with its page.
Privileged package transactions are not delegated to this query adapter.

Settings personalization and its background/taskbar/font subpages come from the
same donor, with existing preference keys retained. The public UI continues to
write ordinary per-user choices; no system theme or font files are overwritten.

The read-only desktop-account ownership parser and its tests come from the same
application donor's shared UI source. They are an internal account-integration
target, not part of the painting SDK. Public system configuration takes precedence
with a legacy fallback; malformed/unreadable records are explicitly invalid.
This display model never grants privileges or replaces PAM/helper authorization.

The reviewed NetworkManager activation helper is extracted unchanged from the
shell into an internal Qt Core/D-Bus target shared with Settings. Existing Wi-Fi
security classification, password validation and activation feedback remain one
implementation rather than diverging copies in the two interfaces.

Settings Apps/Accounts pages are adapted from the same donor. Default-app
publication now validates and atomically updates only selected MIME defaults,
retains other associations and reports failures with UI rollback. Account data
is read through the shared ownership model; password changes still use the
system's interactive passwd command and authoritative authentication policy.

Settings time/language/privacy pages are adapted from the same donor. timedated
reads and authorized changes are asynchronous; controls read back actual state
on success or failure instead of assuming a queued request succeeded. Locale,
layout and shortcut edits check atomic profile writes, and clearing recent-file
history now publishes atomically with visible retryable errors. Regression tests
use a private timedated service and disposable user data, never the host clock.

Settings Storage is adapted from the application donor. Its background physical
walk now reports incomplete reads, assigns overlapping categories once, counts
hard links once and retains allocated-block accounting for sparse files. Cache
cleanup is explicit and asynchronous, rejects redirected roots and reports
errors; conda command paths are shell quoted. Folder/file handoff uses the shared
FileManager1 client, retaining the active desktop's manager.

Settings System is adapted from the application donor. It uses the public shell
D-Bus contract, asynchronous reads and authoritative readback after display,
audio and notification changes. Display preferences are saved only after the
compositor accepts the command; malformed replies and failed changes remain
visible. Private-bus and fake-executable tests never change host displays/audio.

Settings Bluetooth and input devices are adapted from the same application
donor. BlueZ discovery and changes now have deadlines, visible failures and
state readback; disconnected adapters cannot leave stale actionable paths.
Device labels in shared setting rows are plain text. Private-bus tests cover
accepted/denied power and connection changes and adapter/service removal.

Settings Network is adapted from the application donor. It retains escaped nmcli
parsing and the shared Wi-Fi activation/password checks, uses explicit UUIDs for
saved connections and bounded asynchronous commands with visible failures.
Concurrent activation is prevented and superseded query callbacks are ignored;
radio state is read back after failed changes. Fake nmcli fixtures never touch
the build host or QA VM's actual network.

The bounded Settings command adapter and process regressions are extracted to
an internal Qt Core-only application helper for Settings/App Center reuse. Child
queries use C.UTF-8 for stable parsable labels without changing the parent locale.
The helper remains unsuitable for privileged package transactions, whose lifetime
must not be tied to a query deadline or page destruction.

The prototype App Center/Settings APT helper and polkit policy are reviewed as
optional shared software integration. The root helper is rewritten as an isolated
Python entry point with fixed executable/environment, explicit argument limits
and no shell/options/local-file/version inputs. The polkit action retains admin
authorization and applies only to the installed helper path. Software jobs have
application lifetime and a quit lock, bounded retained output and exactly-once
completion, without killing package transactions on a page's query deadline.
References: https://www.freedesktop.org/software/polkit/docs/latest/pkexec.1.html
and https://doc.qt.io/qt-6/qeventlooplocker.html .

Settings Updates/About are adapted from the application donor. APT simulation
is a bounded unprivileged query; list refresh uses the shared application-owned
software job with explicit failure/retry state. Hostname changes are serialized,
request interactive authorization and read back authoritative state. Tests use
fake APT/pkexec and hostnamed services and never change packages or host names.

The complete Settings window, navigation/search, nested pages, theme rebuild,
launcher metadata and Russian catalog are adapted from the same application
donor. Public executable/desktop/translation names use Karyo; explicit compatible
user preference locations remain unchanged. The optional software runtime owns
the APT helper rather than embedding privilege policy in the application UI.

App Center's curated catalog entries are reviewed from the same application
donor: 58 public package descriptions with no private endpoints or profiles.
They are installed only by an optional external data component. Code reads the
same XDG karyo/catalog.json path as Start, validates bounded inputs and executable/
package fields, and refuses malformed or duplicate records without silently
replacing a higher-priority personal catalog with system data.

The App Center package/environment model is adapted from the same application
donor. It uses shared bounded queries and application-owned software jobs,
serializes APT changes, preserves known state on failed reads and reports errors.
Flatpak operations stay per-user; conda prefixes and generated terminal scripts
are quoted, with atomic checked publication. Explicit Miniforge bootstrap now
uses a private temporary directory and the versioned official installer/checksum
assets before running as the current user. See the upstream release asset index:
https://github.com/conda-forge/miniforge/releases/expanded_assets/26.9.4-2 .
Tests substitute every package manager/downloader and never install conda or
contact a real repository during source-level acceptance.

Real installed Settings acceptance found loopback being shown as the active
network and a false disconnected state on networkd/ifupdown QA bases. Network
observation now skips loopback, prefers a default-route interface and falls back
to read-only Qt interface/kernel-route/resolver data in the caller's namespace.
It does not replace or reconfigure an existing network manager. Regression
fixtures cover loopback/bridge precedence, escaped IPv6/DNS and route metrics.

App Center cards, source tags, responsive grids, action/progress pills and
headings are adapted from the same application donor with public UI targets.
External label text is plain text. The card/model regression verifies details,
transaction busy feedback and recovery after a failed install using fake tools.

App Center discovery, categories, installed apps, package search/details, updates
and environment pages are adapted from the same application donor. Optional
catalog absence has an explicit empty state; unavailable/failed update reads do
not claim the system is current. Actions track active transactions. Environment
exports query before atomic publication, and imported paths and environment names
pass through validated model operations. Shared literal shell argument quoting
is exercised with apostrophes, shell substitutions and newlines.

The native full run exposed a traditional-ZIP wrong-password header collision.
A deterministic synthetic stored ZIP and standard-library generator reproduce
it on both bases: the wrong password passes the one-byte encryption header and
fails at the data CRC. Encrypted ZIP decoder/CRC failures retain password retry
while explicitly allowing damaged-data diagnosis; filesystem failures and
cancellation do not become password prompts. Format reference:
https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT .

The App Center window, launch entry and Russian UI catalog are adapted from the
same application donor. It now has a testable independent window, category/search/
details navigation with back, plain-text operation feedback, and theme rebuilds
that retain the model and active jobs. Public launcher names use Karyo and the
catalog remains external optional data. Native route/search/theme regressions
substitute package tools and require no authorization or real package changes.

The independent font runtime contains the prototype's 16 unmodified complete IBM
Plex Sans/Mono TTFs, verified against its recorded upstream payload hashes.
Upstream revision is 71f558992315a12ca1f06a2215d57962a1992162. Fonts retain their
IBM copyright, Reserved Font Name Plex and SIL OFL 1.1 notices; they are not
relicensed under GPL. The component supplies files without global font aliases,
and native Qt tests verify every hash, font family and Latin/Cyrillic coverage.
Upstream license: https://github.com/IBM/plex/blob/71f558992315a12ca1f06a2215d57962a1992162/LICENSE.txt .

The optional greetd IPC transport is adapted from the application's sign-in
prototype at the same donor revision. Native-endian length/JSON framing now has
a bounded payload, validates documented reply shapes and fails closed for
malformed/truncated input. Tests use a private local socket only; no real PAM or
session is started. Protocol reference:
https://manpages.debian.org/trixie/greetd/greetd-ipc.7.en.html .

Sign-in presentation is shared through one internal widgets target for the lock
and optional greeter. Its view/name helpers contain no authentication decisions;
PAM and greetd retain their separate controllers and policies. User names,
account details and authentication messages render as plain text. Existing
layout cases and literal external-text regressions cover the shared view.

The optional greeter authentication controller is newly separated from the
prototype's presentation. It captures the selected session per attempt, answers
arbitrary documented PAM prompts, never sends a pretyped password to a visible
question, and starts only after greetd reports authentication success. It waits
for cancellation acknowledgement before retry and for start acknowledgement
before announcing readiness. Tests use a private fake daemon only, covering
denial, cancellation during authentication, repeated prompts and disconnects;
they make no claim of real PAM or display-manager acceptance.

Greeter user/session discovery is adapted from the application donor. It filters
child desktops and non-login/system accounts, refuses an invalid ownership map,
keeps other installed Wayland sessions and resolves literal executable arguments.
TryExec/DesktopNames use the shared Desktop Entry parser rather than INI comment
semantics. Selection preferences move to bounded atomic Karyo JSON with checked
publication and a bounded snapshot of the legacy INI only when the new file is
absent. No password, token or authentication decision is stored in preferences.

Greeter appearance adoption starts from the donor's published theme/accent
behavior, rewritten to open once without following symlinks, validate the same
descriptor's owner/type/size and read a bounded allowlisted INI subset. It never
opens the selected user's home preferences or deserializes arbitrary variants.
The public Karyo publication path is preferred with compatible legacy fallback;
only the greeter account's own validated appearance keys are updated.

The independent greeter window adapts the donor presentation around the new
controller and inventory. Account/session choices freeze during authentication,
visible and secret questions use the correct input mode, cancellation restores
password entry, and preferences are saved only after the start acknowledgement.
Private-socket widget tests exercise these flows without PAM or session launch.

The optional greeter runtime is now a Wayland-only executable using the shared
style, account/session inventory and greetd controller. Its embedded Russian
catalog is maintained with this frontend. The documentation-only greetd example
installs under share/doc and does not activate a service, create users, replace
a display manager or install an alternative PAM policy.

The real greetd 0.10 cleanup regression was reviewed against upstream
[Context::cancel](https://github.com/kennylevinsen/greetd/blob/0.10.3/greetd/src/context.rs)
and the session worker interface. The controller performs only one extra cleanup
request after explicit authentication denial, then still requires positive
cancellation before another attempt; it does not treat a cleanup error as login
success. Native regressions and actual PAM sessions on both bases verify this.

Public appearance publication adapts the donor shell/main.cpp behavior, rewritten
to derive the account from getpwuid_r, validate public keys and use openat/renameat
through a no-follow directory descriptor. It preserves a previous publication on
failure and never follows a destination symlink. The optional greeter's tmpfiles
data directory is independent of service activation and PAM. Native tests verify
writer/reader interoperability, atomic replacement and unprivileged publication
into a root-owned sticky directory without listing permission.

Native compositor protocol integration is reviewed from the application's donor
revision 26bad17cef413d26c36c117f2f1123dca4edded7, then adapted separately for native
labwc 0.8.3 and 0.9.3. Derivative patches preserve the upstream GPL-2.0-only file
licenses; XML preserves MIT identity/notices and native copyright metadata adds
the full MIT terms. Independent fetch/acceptance tooling is GPL-3.0-or-later.
Ubuntu adaptations use its actual node descriptor/view title API. Explicit
session-lock definitions and a missing-keymap guard remove prototype assumptions.
Native recipes and their source identities are under integrations/compositor;
whole upstream sources and generated binaries remain ignored build inputs.

FileManager1 activation handoff is new common application code following the
standard xdg-activation protocol. Its XML is copied unchanged from the native
Debian wayland-protocols package and retains Aleix Pol Gonzalez/Carlos Garnacho's
MIT terms. Generated code is not relabeled as GPL; each caller runtime installs
the original XML with its full notice. The independent request/fixture code is
GPL-3.0-or-later. QtGui's private native surface interface is compiled separately
on the supported Qt 6.8/6.10 bases; tokens and callback correlation use independent
Wayland resources rather than concrete private QtWayland window classes.

The optional profiles model reviews the donor's desktop-helper/desktop CLI
ownership, protection and /30 address rules at the same application donor
revision. It preserves main/child semantics and legacy read compatibility while
rewriting parsing as immutable, bounded literal policy. Its tests do not create
accounts, invoke PAM, start sessions or configure networks. Privileged mutation,
network, VPN and clipboard runtime migration remains separate.

The owned network primitive reviews the prototype `desktop-netns` UID-to-/30
address layout and intended per-profile isolation. Its implementation is newly
written GPL-3.0-or-later Python using native iproute2/util-linux/procps commands
and Linux namespace descriptors. It does not copy the prototype shell script,
unconditional namespace/link deletion, global forwarding switch, shared NAT
table, resolver selection or Docker firewall exceptions. Full networking and
session supervision remain separate. The installed QA probe is also new own
code; it contains no imported user accounts, tunnel profiles or secrets.

Physical-underlay discovery and owned forwarding are new GPL-3.0-or-later code.
They use reviewed iproute2 RPDB/numeric output, nftables exclusive creation and
native table handles. Linux reverse-source lookup behavior was read from the
upstream v6.12 fib_frontend.c and IP sysctl documentation; no implementation
was copied. The fixture uses only synthetic policies and public DNS/TCP probes,
not imported personal tunnel profiles, certificates or secrets.

Private profile DNS configuration, lifecycle checks and installed QA are new
GPL-3.0-or-later code. Backend discovery was reviewed against upstream NetBird
v0.82.1 DNS sources and systemd resolve1/D-Bus documentation. No NetBird
implementation, account, private server configuration or credential is imported.
The QA fixture uses synthetic .test names and a local DNS responder. Native
systemd-resolved and dbus-daemon remain separately packaged upstream programs.

The offline user-manager scope and complete logind owner inventory are new own
GPL-3.0-or-later code. Upstream systemd.exec PAMName documentation and disposable
native PAM/session experiments inform the process/cgroup model; no upstream
implementation or unchecked prototype lifecycle script is copied. Native
user-manager termination remains owned by logind rather than a forced stop.

The controlled graphical PAM lease, activation wait and entry payload are new
GPL-3.0-or-later code. Native logind/session-scope behavior was verified on both
supported bases. The PAM file includes native common account/session policy and
denies authentication; it copies no prototype unchecked login service, source-
environment shell execution or TTY hangup lifecycle. Optional QA contains no
personal accounts, tunnel configuration or embedded test credentials.


The pre-login NetBird provider primitive is newly written GPL-3.0-or-later code.
It reviews the prototype desktop-netbird unit's separate state/socket/namespace
intent, but does not copy its overlay /etc mounts, host resolver file mutation,
unchecked service activation or cleanup. The upstream NetBird v0.82.1 daemon
flags and private resolve1 integration were reviewed against
https://github.com/netbirdio/netbird/blob/v0.82.1/client/cmd/service.go and
https://github.com/netbirdio/netbird/blob/v0.82.1/client/internal/dns/host_unix.go.
No upstream implementation, management URL, imported profile or credential is
included. Native NetBird remains an external separately licensed dependency.


The ordinary NetBird client endpoint validation, capability reset and asynchronous
browser shim are newly written own GPL-3.0-or-later code. They preserve the reviewed
prototype wrapper's nonblocking browser intent without importing its shell PATH
assumptions, user configuration or credentials. Native NetBird and xdg-utils are
external programs; no upstream implementation is copied into these launchers.


The GUI wrapper's explicit WebKit sandbox-preserving zero value follows review
of NetBird v0.82.1 client/ui/tray_linux.go and WebKit's sandbox switch semantics.
NetBird respects a preexisting environment value instead of applying its coarse
Ubuntu sysctl heuristic. This adapts launch policy without modifying upstream
binaries, globally relaxing userns/AppArmor or importing upstream implementation.
Sources: https://github.com/netbirdio/netbird/blob/v0.82.1/client/ui/tray_linux.go
and https://github.com/WebKit/WebKit/blob/webkitgtk-2.52.6/Source/WebKit/UIProcess/glib/WebProcessPoolGLib.cpp.


The compositor hidden-owner grouping/minimize patches adapt the prototype
wine-owned-window-groups.patch and wine-owned-window-minimize.patch at donor
26bad17cef413d26c36c117f2f1123dca4edded7. They retain labwc's GPL-2.0-only source
licensing, rather than applying this repository's GPL-3.0-or-later default to
third-party derived code. Debian and Ubuntu variants use their native APIs and
libraries. No application binary, personal Wine prefix or settings are copied.
The GPL-3.0-or-later headless XWayland QA tool adapts the prototype regression,
adds four late palettes, package hash verification, ordinary-user isolation and
acknowledged X11 retirement between cases. Python Xlib, Pillow, grim and native
Xwayland remain external test dependencies with their own licensing.


The independent Windows launcher adapts the prototype openkaryon-win32 Python
launcher at donor26bad17cef413d26c36c117f2f1123dca4edded7. It is split into own
GPL-3.0-or-later policy/storage/registration/frontend modules. Per-account storage,
ordinary credential validation, capability reset, bounded owner-checked records
and Karyo-scoped MIME defaults replace the obsolete two-slot environment model.
No personal prefix, registered application or binary is imported. Native Wine,
PyQt6 and icoutils are separately packaged dependencies with upstream licenses.
Source policy and real native engine/package acceptance have distinct gates.


The native Windows acceptance fixtures are new GPL-3.0-or-later C, RC, WiX XML
and Python QA code. They compile a synthetic PE64 and per-user MSI using native
MinGW and wixl, with a generated single-color icon. They introduce no Windows
application, proprietary resource, imported prefix or credential. Native Wine,
MinGW, wixl, Python Xlib, Qt, Pillow and grim retain their own licenses. Actual
Wine64 runtime and native Qt dialog tests remain distinct from the compositor's
modeled owned-window regression and from pending full application compatibility.


The Win32 menu and persistent private Wayland pointer probes are new own
GPL-3.0-or-later QA code. The unmodified wlr-virtual-pointer-unstable-v1 XML
retains its MIT license and Josef Gajdusek copyright, at official upstream
c11408942e2fb54d41dadb84cdf844331076ae11. Its URL/hash are recorded beside the
XML. These fixtures and protocol data are not installed in runtime packages.


The standard-menu engine patch adapts the prototype's x11-popup-menu-type.patch
at donor26bad17cef413d26c36c117f2f1123dca4edded7. The original reviewed file SHA256
is e3c20df0962946899b8ead8147a734342dd701fc001ddabc090255bc665bbe3a;
its body was rechecked against that exact Git object. Changes to Wine-derived
source retain LGPL-2.1-or-later, not this repository's own GPL-3.0-or-later default.
Native Debian/Ubuntu source and binary packages retain their copyright files.
The initial menu recipes preserve native control dependencies and packaging.
The later reviewed caption addition is documented separately below. Generator data from a signed Ubuntu snapshot is confined
to source/QA preparation and is not a foreign native-library runtime transplant.


The native Wine coinstallation inspector and tiny-DEB negative fixtures are own
GPL-3.0-or-later QA. They read native package control/payload streams with dpkg,
compare shared paths without extracting into a live system, and add no runtime
service or privilege mechanism. Wine32 reuses the exact reviewed source/patch and
changelog epoch of the Wine64 recipe. SDK/dependency preparation remains isolated
from product and host package configuration.


### Native wlroots lifecycle integration

The optional compositor integration rebuilds native Debian wlroots 0.18.2-3 and
Ubuntu wlroots 0.19.2-1 from their distribution source archives, with exact dsc
and archive SHA256 values in integrations/compositor/recipes/*-wlroots.json.
The fetcher uses signed native APT metadata and verifies every captured part.
The shared association correction was derived after comparing actual Xwayland
protocol traces with each release's xwayland/xwm.c: a buffer could commit before
the association installed its commit listener.

Debian's FocusIn acknowledgement adjustment adapts the focus-state bookkeeping
in Ubuntu's exact wlroots 0.19.2 source (original archive SHA256
804bd3eab10f03556628c4e977351b0b1b17bc34084342dfaff5086c54ce1fa0).
No public struct or exported ABI is changed. Ubuntu-only libinput-switch and
xcursor const compatibility changes preserve strict native compilation. These
are derivatives of MIT wlroots, with its full notices retained separately in
integrations/compositor/patches/WLROOTS-LICENSE and in native package copyright.
They are not relicensed under the surrounding repository's GPL-3.0-or-later.
Own acceptance tooling and Win32 fixtures remain GPL-3.0-or-later.


The later unmapped-owner slice adds native MIT XWM parent-hint refresh patches.
They are derived from each distribution's already checksummed xwm.c and reuse
its parsers when a transient references an unassociated parent. No public ABI
is extended. Explicit owner classes remain authoritative; only verified missing
classes may use labwc's same-PID visible-sibling fallback. Native packaging
requires the matching karyo2 library; original packaging notices remain intact.
The independent real-X11/Wine acceptance code stays GPL-3.0-or-later.


The default owned/child caption close-button patch reviews the prototype's
child-close-button.patch at donor26bad17cef413d26c36c117f2f1123dca4edded7,
original SHA256 bf5a30f00d307ece05052f64e1037b9e7b59d157a66d9448fc982d0fd6dec586.
The adaptation saves/restores the whole GDI DC state, forces copy-pen drawing
and preserves the native disabled-button path. It retains LGPL-2.1-or-later
like Wine's defwnd.c; native package copyright remains authoritative. New C
caption pixel probes and Python actors are own GPL-3.0-or-later QA, not runtime
payloads or third-party application resources.


The Debian absolute-pointer patch adapts src/input/cursor.c behavior from native
labwc0.9.3 (archive SHA256 deda3c37098b03eccbe4cc82662efde96570981910f1ae910f18156cbbaa231e).
It removes relative-motion delivery from the absolute handler only, retaining
the native relative handler and GPL-2.0-only upstream source notices. The before/
after Win32/X11 pointer traces use identical own QA producer and actor bytes,
with unchanged Wine2 and native wlroots. No third-party public ABI is added.


The implicit child-region patch reviews rounded-child-windows.patch from donor
26bad17cef413d26c36c117f2f1123dca4edded7, original SHA256
8828f233d253faf99f705890c808cb7c6531f4a4929b3b344086337e116ed3ca.
It adapts Wine server/window.c under LGPL-2.1-or-later, retaining native package
copyright. The default captioned-child shape is cached separately from the
application's explicit region and follows native DPI. An additional correction
preserves old visible coverage before shape-changing styles so restored MDI
icons do not leave corner pixels behind. GetWindowRgn's explicit-region API is
unchanged. The original child/MDI producer and private-pointer/pixel actors are
own GPL-3.0-or-later QA. They contain no Windows application or personal prefix.


### Native scene mask extension

The native wlroots scene-mask patches review scene-clip-region.patch from donor
26bad17cef413d26c36c117f2f1123dca4edded7, original SHA256
fc18e9a3959178bd5a2b419ef2ea17d85fffce8ee1d207fb246d4bef06ece3a4.
They retain MIT with the original wlroots notices and native package copyright.
Masks use the existing node addon lifecycle and add one exported function;
public structures are unchanged. Native 0.18 and 0.19 direct-scanout adaptations
retain each release's unclipped behavior and reject masked full-buffer scanout.
The new original renderer/input/ABI C fixture and Builder acceptance driver are
GPL-3.0-or-later QA. They use native headers/libraries without incorporating a
third-party application, private prefix or proprietary image.


### Per-view Windows compositor theme

Native labwc derivatives review windows-titlebars.patch (SHA256
 a739ce9cd7d56f73180f7f7ed3971b643b4f86d37ade826bb4e99f9295f9c022)
and wine-window-frame-selection.patch (SHA256
 3648455008f2694e0ee49f4685d01baff4f16d2598cfdd9f3de3eb0c63633e25)
from distro/distro/patched/labwc at prototype
26bad17cef413d26c36c117f2f1123dca4edded7. Actual Git blobs were checked against
the reviewed files. Patches retain GPL-2.0-only and upstream C/header notices;
the root own-code license does not override the compositor's license. Ubuntu
adaptations use its native background patterns, SSD arrays and button module.
The original mixed-theme/late-class/reconfigure QA is GPL-3.0-or-later. No donor
application, personal path, executable resource or private prefix is incorporated.


### Windows caption controls

The native caption geometry patches review the width/layout/hitbox and asset
lookup sections of wine-windows-caption-details.patch from donor26bad17,
SHA256a72d24b57e366beab910d199be3149961c5815036fa90accff6e5f10e6a1a29f.
The actual Git blob was checked. Native derivatives retain GPL-2.0-only and
upstream notices. Caption rounding portions remain a subsequent step.
The independent SVG glyph resources, CMake installation and pointer/pixel actor
are original Karyo code/resources under GPL-3.0-or-later, without copied binary
artwork, Windows fonts or application-specific configuration.
