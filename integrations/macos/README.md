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


The33 wrapper regressions pass without skips as ordinary UID1001 on native
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
