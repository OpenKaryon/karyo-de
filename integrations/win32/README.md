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


## Reviewed native Wine menu patch

wine-recipes/{debian,ubuntu}.json and wine-patches/x11-popup-menu-type.patch
rebuild the selected native Wine source with Builder's source-package stage.
The derived engine patch retains LGPL-2.1-or-later, separate from this launcher's
GPL-3.0-or-later code. Only the standard PopupMenu system class atom32768 changes:
its X11 type becomes POPUP_MENU and it never requests keyboard focus. No app
name, registry profile or private prefix is consulted by this engine patch.

Prepare the matching native source-builder/compositor/Win32 QA images as above,
then prepare the Wine build image from this checkout:

    docker build -f integrations/win32/environments/debian/wine-source.Dockerfile -t karyo-wine-source-builder:debian integrations/win32/environments/debian
    python integrations/win32/fetch-wine-inputs.py --base debian --image karyo-wine-source-builder:debian
    karyo-builder source-package integrations/win32/wine-recipes/debian.json --image karyo-wine-source-builder:debian --output out/wine/debian/menu-packages --jobs 2

Ubuntu uses its own environment, recipe and base name ubuntu. Existing source
inputs are verified before any download; different existing bytes are preserved
and rejected. Fresh downloads use signed native APT metadata and must match the
recipe's complete DSC/archive SHA256s. The actual source build has no network.
Native copyright/control dependencies and normal packaging remain intact.

Ubuntu Wine10 needs older Unicode generator data than the current archive.
Only unicode-data, unicode-idna and unicode-cldr-core may come from the signed
Ubuntu snapshot20250417T000000Z; every other package from that snapshot is pinned
out. Native ABI libraries remain resolute. This source/QA preparation does not
change runtime or host APT sources. Reference: https://snapshot.ubuntu.com/.
Native Wine packaging itself disables upstream tests; real executable/menu
acceptance is required separately and must not be described as those unit tests.

Builder's installed Win32 acceptance driver takes --wine-package-directory for
the successful native output and installs a checksum-verified runtime subset.
The --pe32 option additionally tests Ubuntu's native WoW64. Native Debian Wine32
uses a separate i386 source build and still requires matching package/installation
acceptance before that architecture can be called complete. Engine packaging,
ordinary-user launch, compositor palette behavior and caption styling remain
separate gates.


## Matching native Wine32 on Debian

Debian13's legacy Wine64 and Wine32 need the same native package version and
shared Multi-Arch:same payload. Build the i386 SDK with the prepared Builder
native386 base, then reuse the checksum-locked Debian source inputs:

    docker build --platform=linux/386 -f integrations/win32/environments/debian/wine-source-i386.Dockerfile -t karyo-wine-source-builder:debian-i386 integrations/win32/environments/debian
    karyo-builder source-package integrations/win32/wine-recipes/debian-i386.json --image karyo-wine-source-builder:debian-i386 --output out/wine/debian-i386/menu-packages --jobs 2

The32-bit recipe keeps the64-bit source, patch bytes, revision and changelog
epoch. tests/check-wine-shared.py compares actual native control identity and
streams both libwine payloads, rejecting differing shared content, types, mode
or ownership. Real tiny-DEB negative fixtures cover mismatched version,
architecture, content and mode. The actual matching pair agrees on88 shared
files, with separate786 amd64 and845 i386 paths.

runtime-i386.Dockerfile prepares only a disposable amd64 QA image with stock
32-bit dependencies and native i686 MinGW for our own fixtures. It changes no
host or product APT configuration. Builder's installed acceptance takes
--wine-i386-package-directory together with the64-bit output and --pe32. It
checks matching source/version/patches/epoch, image ancestry and artifacts,
compares shared files before unpacking, and installs both runtime architectures
in one transaction. Only runtime packages are selected; all/fonts packages come
from the64-bit build.

Real PE32/MSI32 and PE64/MSI64, private-user controls, registration/icon/Unicode
and keyboard/persistent-pointer menus pass with this matching Debian pair.
Existing-user deployment and legacy-prefix import remain separate from this
clean-image installation gate. Actual owned palettes and Windows-style frames
also retain their own acceptance; these launch tests do not establish them.


## Real owned-palette acceptance

The separate palette-probe.c fixture is a real Win32 program compiled as PE32
and PE64. It creates one main frame, four colored owned tool palettes (including
two created later), a tiny no-activate framework owner and an unrelated frame.
No proprietary executable, application-specific Wine option or registry setting
is used. The `--mapped-owner` variant keeps the helper at 1x1, as in the reviewed
legacy framework model. Leaving that helper fully unmapped exercises the same
real ownership hints after native parent-metadata refresh. WM_TRANSIENT_FOR
is preserved in both variants.

palette-acceptance.py runs as a disposable ordinary user in a private headless
labwc/Xwayland session and fresh Wine prefix. It waits for actual compositor
client publication and main-window paint before testing pixels. The scenario
checks all four palette colors above the main frame, unrelated-frame occlusion
and return, scene minimize/restore without X11 IconicState or process exit,
stable focus and normal WM_DELETE_WINDOW close. A real private Wayland pointer
click establishes input focus before testing an unsolicited focus change by a
separate X11 process. That process must not retain Wine input focus. Programmatic
focus offers alone are not assumed to be accepted by a globally active client.

Builder tests/acceptance/check-win32-palettes.py captures successful native
runtime evidence, freezes both fixtures, validates and installs matching native
compositor/wlroots DEBs, then runs repeated cases without network, capabilities,
a writable system root or an existing user display/bus/home. It preserves every
failed attempt separately. Both native bases pass PE32/PE64 with the corrected
role lifecycle; see Builder docs/VALIDATION-WIN32-PALETTES.md for exact records.
The current pair is Debian labwc karyo5 / native wlroots 0.18 karyo2, or Ubuntu
labwc karyo4 / native wlroots 0.19 karyo2. Both executable bitnesses pass with
mapped and fully unmapped owners. Earlier checkpoints retain their own exact
artifact identities separately.

The unmapped variant requires the matching native library: a missing cached
class can otherwise hide an explicitly different WM_CLASS set before XWM event
subscription. Library karyo2 refreshes identity and decorations when that parent
is referenced; the compositor's exact dependency enforces the verified pair.
Use --verify-library-dependency to exercise refusal of a compositor-only upgrade
before the successful paired installation in a disposable image. Ten X11 models
keep explicit foreign classes, foreign processes and ambiguous frames separate.
Legacy-prefix import, existing-user upgrade, full taskbar association,
Windows-like frame/child styling and actual third-party applications remain
separate gates. These are headless software-rendered checks, not physical GPU,
VMware host integration or full DE migration completion.


### Existing prefix architecture

New per-application prefixes still select win64. When a selected prefix already
contains system.reg, the launcher removes inherited WINEARCH and lets Wine
validate its existing architecture. It does not parse/rewrite a registry or
convert a win32 prefix into win64. An incompatible native engine can still
reject the prefix; this is not acceptance of legacy-prefix import or conversion.
The source regression covers win32, win64 and an older unannotated registry,
preserves their bytes and verifies new-prefix behavior after removing the marker.


The installed package from 236dd5a also passes a real existing-prefix check.
Debian's matching multilib Wine creates a native win32 prefix: the old launcher
rejects it twice for the forced win64 mismatch, while the new package runs the
same PE32 twice. Ubuntu's native WoW64 runs PE32 twice in an existing win64
prefix. Both preserve the architecture marker and a pre-existing data file.
The actor waits for native wineserver to flush initialization before inspecting
registry metadata, discovers Ubuntu's private server through native package
contents and runs without a host display, credentials or personal prefix.
This does not establish pure win32-prefix compatibility on Ubuntu, prefix
import/conversion, a new Wine engine or a complete joined current-HEAD desktop.


### Optional native caption engine

The reviewed LGPL Wine patch now fills enabled default child, tool and owned
close buttons with a red square and white cross. It saves/restores the whole
GDI DC state and leaves the native disabled-button path in place. Applications
which paint custom captions and compositor-provided main frames are separate.
No program-name rule, imported prefix or per-application registry option is used.

All three native Wine source recipes now build karyo2, retaining matching Debian
amd64/i386 source and epoch. Their coinstallation still proves all 88 shared
libwine files identical. Debian13 and Ubuntu26.04 each pass eight runtime/menu
stages, four real hidden-owner palette runs and PE32/PE64 caption pixel checks.
The same frozen source/actor before/after verifies solid fill rather than only
a red outline; default disabled and ordinary unowned buttons remain unchanged.
The own QA fixtures are not installed in runtime packages. Native packaging
retains Wine's LGPL notices and existing upstream test configuration.

The pixel gate covers unpressed default buttons at the captured native metrics,
not arbitrary themes, DPI, custom captions or physical GPUs. Rounding, caption
metrics/icons, legacy import, real applications and complete installed-user
upgrade remain separate. The runtime/menu launcher snapshot remains e782fb0;
this engine acceptance is not a joined current-HEAD desktop installation.
