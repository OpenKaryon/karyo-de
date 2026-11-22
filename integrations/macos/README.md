# Optional Intel macOS integration

The optional karyo-macos-core package provides the independent karyo-macos CLI inspects Intel x86_64 Mach-O executables and .app
bundles without executing them. It can launch through an explicitly installed
Darling engine, forwards literal arguments and records private diagnostics.
This optional wrapper neither installs nor enables a runtime. The runtime and
actual application compatibility require separate native acceptance.

    karyo-macos --status --json
    karyo-macos --inspect '/path/Example.app'
    karyo-macos '/path/Example.app' -- --application-argument

Raw ARM executables, ELF/PE binaries, dylibs, .pkg installers and .dmg images are
rejected. Bundle metadata must be bounded regular Info.plist with a safe
CFBundleExecutable; the resolved executable must stay inside the bundle.

The account database controls HOME/USER/LOGNAME and persistent storage. Old
two-slot variables cannot select another desktop's prefix. Each canonical
application path and display pair gets a distinct short profile under
~/.karyo-macos. Darwin's socket path limits the complete prefix to79 UTF-8 bytes;
long paths fail before creating state. Existing prefix data is not recreated.
If home is overlayfs, only a private owned XDG_RUNTIME_DIR on tmpfs is accepted;
that fallback lasts for the login session. Prefixes separate runtime state,
not filesystem access: Darling exposes host data at /Volumes/SystemRoot.

Darling must initialize the leaf itself, so the wrapper creates only private
parents/logs. Ancestor symlinks, foreign-owned prefix directories and unsafe
metadata files are rejected. Inspection creates no state. Graphical launches
use a fixed shell script and positional user arguments, scoped10.13.6 metadata,
QT_MAC_WANTS_LAYER=0 and Fusion by default. KARYO_MACOS_QT_STYLE=native leaves
the native style; any other value fails before storage writes. Linux Qt/QML
variables and loader injection variables are removed. Display/auth remain;
DARLING_SHARE_X11_IPC=1 is enabled only for a real X display as required by the
reviewed optional runtime. This mode shares SysV IPC with that display.

The wrapper refuses root or mixed/saved credentials. Before executing the engine
it clears inherited ambient/effective/permitted capabilities, retaining the
separate engine's own privilege bootstrap; it does not install a privileged
wrapper. Existing NoNewPrivs policy is never disabled. The engine package owns
its setuid/helper audit and is not supplied or proven by wrapper policy tests.

The CLI streams actual output and preserves child exit status. A persistent
server holding stdout cannot keep a completed application wrapper blocked; live
applications do not acquire a timeout from that bounded post-exit drain. Logs
are exclusive0600 files. Runtime installation or exit0 never establishes GUI
or arbitrary application compatibility. Shared Qt picker/MIME and native engine
packaging/actual runtime acceptance are later pieces of this migration.


The wrapper suite now has35 cases, passing without skips as ordinary UID1001 on native
Debian13/Python3.13 and Ubuntu26.04/Python3.14. The separate CMake component also
passes its test entry in both normal Builder package environments, exercising
root refusal instead of claiming root is a supported application caller. These
checks include actual Linux child processes, daemon-held output pipes and a
fixed-script copy of synthetic version metadata; no Mach-O application executes.


The regular native core packages from022644e also pass installed public-entry
checks on Debian and Ubuntu: no-engine status/diagnostics, noncreating inspection,
real-account authority despite HOME/slot spoofing, root and foreign-format
refusal, isolated Python entry, literal arguments/exit23, cleared inherited
capabilities with unchanged NoNewPrivs, private exclusive logs and unchanged
existing file/inode. tests/installed-core-acceptance.py labels its Linux engine
fixture explicitly; it does not execute a Mach-O application. Builder's matching
VALIDATION-MACOS-CORE.md report records package/image/source hashes, fixtures and
remaining shared Qt UI/MIME and actual engine gates.


## Shared Qt picker and MIME integration

The optional karyo-macos package adds the Qt6/C++17 karyo-macos-picker frontend
and depends on karyo-macos-core>=0.1.0-2. Core --gui and invocation without a file
open that configured frontend; a backend-only installation gives an actionable
missing-frontend message. Both command paths forward filenames and arguments
literally. The separate core keeps its Python-only dependency boundary.

The picker uses shared Karyo window/style/widgets, app identity/icon and29 Russian
messages. Status and application inspection run asynchronously with an8-second
metadata timeout and bounded responses. Actual applications have no UI timeout.
Errors remain plain inline text with details/output and a log-folder action;
a temporary live-session profile gets an explicit notice. Launch controls remain
unavailable during checks or execution. Close requests while an application is
running keep its controller open; closing during metadata checks cancels only
that nonexecuting probe. UTF-8 decoding spans output chunks, and both line count
and total text size are bounded.

The desktop entry registers only application/x-karyo-mach-o with thin Intel
executable magic. Recognized Mach-O files open the picker with that selected
application. .app folders are selected inside the picker; ordinary folder
associations remain unchanged. MIME handler registration supplies the default
candidate without overwriting user preferences or another optional component's
karyo-mimeapps.list. No shared MIME/default file is owned by two packages.
The original app icon uses Karyo colors; embedded common Lucide assets retain
their upstream notice in the GUI package.

Source-level native Qt checks cover responsiveness, refresh after a missing
engine, malformed/oversized/timed-out metadata, failed helper startup, inspection
errors, close protection, literal arguments, temporary-state notice, automatic
selected-file launch, inline exit23 errors, chunked UTF-8, bounded long output
and closing before a check completes. Linux helper fixtures are explicit and do
not establish Darling compatibility. Full installed frontend/MIME/runtime results
are recorded separately by Builder when those gates complete.


Frontend CLI arguments use `karyo-macos-picker -- APP [ARGS...]`: every
positional value after APP is an application argument, including a literal
leading `--`. The core's `--gui APP -- [ARGS...]` strips its own separator once
and forwards the remaining values. QApplication receives only the controller's
program name; it cannot consume the application's `-platform` or `-style`
arguments. Controller platform selection remains available through the normal
Qt environment variables. The installed acceptance actor tests a leading `--`,
Qt-looking options, quotes, Unicode and embedded newlines through the public
GUI path; these cases exposed the original constructor/parser regression.


The final core/frontend0.1.0-2 packages pass complete74-entry native builds on
both bases and an installed five-stage private Wayland/MIME gate with the actual
Karyo shell. Public --gui/no-argument entry, Russian title/body, panel icon,
GIO file recognition/opening, user override preservation and Win32/default-folder
coinstallation all pass. A native close request keeps the same controller and
its explicitly synthetic Linux engine alive until that fixture finishes.

A stronger installed before-control proved the old frontend consumed the
application's invalid Qt platform option and aborted on both native bases.
Final after-controls preserve a relative leading-dash Unicode filename, a literal
leading --, -platform/-style values, quotes and newlines through the real public
GUI entry and backend. The source Qt suite reports17 results including
init/cleanup; all35 ordinary-account backend cases remain accepted. Builder
VALIDATION-MACOS-PICKER.md records exact native packages, screenshots and retained
before failures. These GUI/argument/MIME results still do not execute a Mac
application or establish actual Darling compatibility.
