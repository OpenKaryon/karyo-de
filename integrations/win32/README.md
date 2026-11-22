# Optional Windows application integration

Independent ordinary-user EXE/MSI launcher, application prefixes, menu entries,
nonexecuting PE icon extraction and private diagnostic logs. Wine is supplied by
the native distribution; no engine, private prefix or Windows binary is bundled.
The package is optional and does not change global MIME defaults or services.

Home, Work and additional desktops already have separate Unix identities. Each
account owns ~/.local/share/karyo/windows/prefixes/<application-id>. The old
OPENKARYON_SESSION_SLOT environment does not select another desktop. --app-id
shares an installer profile with its program; an EXE under that prefix inherits
its existing application ID. --registered uses a private bounded owner-checked
record instead of interpolating executable paths into desktop Exec. --inspect
reports the selected context without creating storage or executing Wine.

Root and mismatched real/effective credentials are refused before GUI or engine
startup. Real current capabilities are discarded and NoNewPrivileges is set.
Native display, D-Bus and network/mount namespaces are inherited; Linux Qt/Wine
loader overrides are scrubbed before the Windows runtime. Wine remains an
ordinary user process, not a new security sandbox. Diagnostic logs exclude the
application argument list and have private modes. Existing private prototype
profiles are not automatically read, copied or modified by this new component.

The CMake component win32-runtime builds independently without Qt development
libraries. GUI uses optional native PyQt6 dialogs and the session's Qt style.
MIME associations are Karyo-specific data defaults, leaving explicit user and
other-desktop preferences in place. Runtime packages need Python, PyQt6, Wine,
Xwayland, icoutils and native MIME/desktop cache tools. 32-bit engine preparation
belongs to the selected native distribution, not an implicit host APT change.

Source policy tests and installed real-engine/GUI acceptance have separate
records. The implementation adapts the prototype launcher at donor26bad17;
new ordinary-account storage and validation code is GPL-3.0-or-later. Native
Wine, Qt, icoutils and freedesktop components retain their own licensing.

## Disposable native acceptance

Prepare the Debian/Ubuntu QA images from environments/<base>/Dockerfile after
building the corresponding native compositor-builder image. These images add
only synthetic QA users, native Wine64 and MinGW/wixl fixture compilers; they are
not product images. tests/installed.Dockerfile installs hash-checked native DEBs
and compiles our own PE/MSI, refusing a root GUI launch without executing Wine.

Karyo Builder's tests/acceptance/check-win32-runtime.py takes the native package
manifest, native compositor DEB and QA image, then runs the real engine, Qt and
second-account/MIME actors. Each stage has a fresh ordinary-user home, private
D-Bus, no network, read-only root/source and no capabilities. The engine starts
its own headless compositor/Xwayland, never an existing graphical session.

The own C/RC/WiX fixture and Python actors are GPL-3.0-or-later. They contain no
proprietary program or Windows resources; the icon is a generated solid square.
Wine64 acceptance does not claim 32-bit engine support, real application menu
compatibility, caption styling, simultaneous desktop sessions or host file
transfer. Those retain separate product gates.

## Actual standard-menu QA

The own menu-probe.c creates three ordinary Win32 menus. menu-acceptance.py
observes actual Xwayland window types/input hints and can navigate with keys or
with a click followed by repeated Wayland pointer moves. pointer-probe.c keeps
one virtual pointer alive for the whole mouse sequence; its display is owned by
the actor and never taken from the user's session. The unmodified MIT protocol
XML is pinned under tests/protocols with upstream provenance.

Baseline mode requires an actual observed non-popup menu; empty observations are
not a successful reproduction. It does not claim that mouse navigation fails
when only the window type is wrong. Positive mode additionally requires popup
protocol metadata and repeated navigation. Full actual acceptance stays separate
from the older compositor's modeled palette grouping test.


The real-engine and menu actors now accept --bits 32 (default64), validate the
actual fixture PE machine type and retain that bitness in the receipt. Own
PE32/MSI32, Unicode/GDI/icon/registration/prefix/exit status and both keyboard/
mouse menus pass on stock Ubuntu26.04 Wine10's native WoW64 without i386 system
packages. Debian13's legacy Wine64 still requires a matching32-bit native runtime
for that gate. These results do not replace the separate patched-engine checks.
