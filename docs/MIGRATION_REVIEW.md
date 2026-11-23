# Remaining DE migration review

User-approved overnight work: migrate the remaining prototype capabilities into
the independent DE, test Debian and Ubuntu, make local reviewable commits and
**do not push**. Forge stays under its separate maintainer. Baselines before this
work are DE 8ef17c0 and Builder a343953. The donor is read-only during extraction.
The long-running task remains active until its remaining scope is implemented
and verified; this checklist must not be read as a completion claim.

## Current review map

The detailed notes below are chronological checkpoints. Their earlier pending
labels describe the state at that checkpoint; later installed acceptance can
supersede them. Builder docs/MIGRATION.md and its linked validation reports
record exact native inputs and executed boundaries. A component milestone is
not a joined current-HEAD installation or a completion claim.

Shared UI/session/lock, fourteen application families, profile account/password
and isolated network infrastructure, family clipboard, tested native Wine
runtime/styling/application operations, VMware coordinator/lock and activation-
time display following have their separate native/installed milestones. The
optional system HGFS gate now also passes real host file exchange and captured
mount/service/package lifecycle on both native kernels. Live Work NetBird now
passes two actual Home WireGuard disconnect/reconnect cycles on each base,
with ordinary DNS/HTTPS, private peer payloads and complete restoration.
The public Work exit-node packet gate now passes both routing roles alongside
Home WireGuard on both native guests, with encrypted counters and restoration.
Complete logind inventory also survives a real disappearing SSH/PAM login.
The first Debian Darling native build/audit/package gate now succeeds; actual
Mac execution and the Ubuntu engine build remain separate ongoing gates.

Still required for the complete remaining migration:

- Finish Ubuntu Darling engine build/audit/package work and real Mac CLI,
  Cocoa/Qt, privileged bootstrap, prefix/upgrade, multi-account and visible GUI
  acceptance on both native bases. Source/patch/compiler controls alone do not
  establish a working runtime.
- Establish enabled VMware host clipboard payloads, file drag/drop, other-DE
  ownership interaction, real host-driven resize and monitor layout, and their
  complete Home/Work integration. HGFS private-mount inheritance does not prove
  the whole graphical channel matrix.
- Resolve the external ExternalProvider broker/firewall contract and test simultaneous
  Work exit-node DNS/HTTPS with a Home tunnel, physical DHCP/DNS/networkd
  lifecycle and onward NetBird GUI/tray behavior. The accepted kernel WireGuard fixture
  does not substitute for the external client.
- Deploy and test a joined current-HEAD package set on Debian and Ubuntu,
  including applicable session/component lifecycle and preserved existing users,
  data, display-manager and other-desktop behavior.

External portals remain intentional dependencies. Unavailable physical camera,
audio/GPU and hardware-specific scenarios must retain explicit limits; VM
fixture passes are not evidence for absent physical devices. Distribution image,
boot/installer/browser defaults and Forge stay with their separate products.

## Historical system integration checkpoint

Account provisioning, isolated privileged add/sync helpers, global PAM password
synchronization and trusted Qt policy readers have native installed acceptance
on both bases. Shared versus separate password behavior, administrator PAM edits
and package removal/upgrade are verified independently of desktop switching.

The installed initial protected-session gate from 156d1aa also passes both QA
VMs: root-mandated startup uses native labwc +karyo2 locked before clients; real
PAM wrong-password denial, Super/Print suppression, locker crash, retry and
correct-password entry preserve the no-clients-before-authentication rule.
A fixed-host metadata-only driver is profiles/tests/installed-startup.py.
Builder VALIDATION-PROTECTED-STARTUP.md records exact artifacts and limits.

A later VT-return check found an open Ubuntu output-lifecycle defect: vmwgfx
removes/recreates Qt outputs and the old locker gives the replacement output
only a decorative view. The compositor remains locked, but the password form
is missing. Debian returns and unlocks successfully. Generic lock-screen output
handoff is being corrected; initial startup acceptance must not be interpreted
as complete protected desktop switching. Test metadata was removed, and both
VMs finish at their original LightDM with unchanged account/PAM databases.

The output-lifecycle correction now passes native source-overlay QA on both
VMs: replacement outputs retain an interactive form, incorrect passwords fail,
and a correct password returns to the same shell. Ubuntu also passes output
replacement during a pending PAM failure. Temporary binaries/wrappers/metadata
are removed with exact hash checks and package verification stays clean. The
new final lock package is still a separate gate; installed 156d1aa alone retains
the documented output-lifecycle bug.

Final lock DEBs from 283cce4 now pass actual installed initial PAM and VT return
on both guests, with interactive forms, rejected wrong passwords and correct
unlocks preserving shell PIDs. No source overlay is installed. The repeatable
QA VT driver includes all logind session classes before using the empty console
and restores the original VT even on failure; readiness alone is not acceptance.

A separate full-session check found that the Ubuntu taskbar becomes invisible
after output replacement although the shell can still open its menu and real
applications. Shell layer-surface output lifetime therefore remains open.
Actual graphical pkexec already accepts own-family listing and current-desktop
noop switching on both guests, complementing native SSH identity rejection.
That does not close real two-profile switching or its protected entry gates.

The existing-session switch core and fixed active-caller list/switch helpers
are source-tested (120 Python cases on each base, including ordinary UID runs).
Installed two-session authorization and shell wiring remain separate gates. Controlled startup of stopped sessions, VT allocation,
network namespaces/DNS/VPN/capability reset and family clipboard remain required.
The compatibility backlog also includes actual Win32 and experimental macOS
launchers/MIME/runtime integration, VMware session/clipboard/DnD/hgfs agents,
and the remaining Wine/VMware compositor changes. These are not just visual
patches. The donor style plugin is a QStyle plugin, not a separate platform-theme
plugin; external portals remain intentional dependencies. Forge is untouched.

## Boundaries and current state

| Capability | State / next acceptance |
| --- | --- |
| Shared UI, shell, lock, selectable session | Migrated. Clean Debian and Ubuntu VM login/lock/sleep/portal/auth/upgrade/removal and Xfce coexistence accepted; later app/integration checks remain. |
| Debian/Ubuntu native package stage | Added Ubuntu 26.04 build environment and exact base identity validation. Both native fixture packages built successfully; host tests 17/17. |
| Qt compatibility | Explicit private Wayland import for Qt 6.10; Debian Qt 6.8 path retained. |
| Terminal | Independent runtime migrated. Native PTY/Ctrl+link and real installed command execution accepted on both bases; the later tooltip close-button fix also passes with installed packages. |
| Camera | Migrated with independent runtime and public UI. No-device controls/close pass on both bases (14/14 total tests); physical photo/video/audio acceptance pending. |
| Photos | Migrated with atomic rotation publication and FileManager1. PNG pixels and failed-write preservation pass on both bases (15/15 total). GUI/package checks follow. |
| PDF Reader | Migrated tabs, search, remembered positions and compatible IPC; native real-PDF/render/second-process tests pass on both bases (22/22 total). Graphical/encrypted-file checks follow. |
| Player | Migrated library/queue/video surface and compatible IPC. Actual PCM decoding, natural sorting and second-process tests pass on both bases (24/24 total). GUI/video-device checks follow. |
| Calculator | Core engine and complete UI migrated with shared history and existing scientific/lab modes. Native regressions pass on both bases (17/17 total); GUI package checks follow. |
| Clock | Migrated timers/stopwatch, persisted deadlines and notifications. Fake-clock/restart regression passes on both bases (18/18 total); audio/GUI checks follow. |
| Editor | Complete independent window, compatible recovery/IPC and lossless encoding choice migrated. Undo, malformed metadata, both dialog choices and real second-process reuse pass on both bases (21/21 total). GUI package checks follow. |
| Monitor | Complete sampler/UI migrated. Reset/PID/model, responsive services, user scope, atomic startup and disposable-child controls pass on both bases (26/26 total). Real authorization/package checks follow. |
| Files | Migrated with FileManager1, tab reuse, inline rename, USB, folder cache and safe quick archive actions. Both installed VM packages show correct selected-folder size/counts. Full native set 38/38; actual background-window activation remains an integration gate. |
| Archive | Complete backend/window migrated with real ZIP/7z/encrypted RAR, compression, cancellation and atomic publication. Password retry/cancel and creation/browsing GUI cases pass on both bases (31 current CTest entries). Native package/VM checks follow. |
| Snipping | Complete canvas/window/overlay migrated with public shortcut routes and compatible preferences. Native tests pass on both bases (28/28 total). Real overlay/clipboard and optional compositor window-snapshot integration follow. |
| Settings | Complete source/runtime with all public routes and 305 initial Russian messages. All routes/runtime migrated. Real authorization, APT refresh and networkd/ifupdown observation accepted from native DEBs on both VMs. |
| App Center / catalog | Complete independent runtime, pages/window, external validated catalog and APT/Flatpak/conda model migrated. Installed native DEBs accepted for discovery/search, authorized ncdu install/open/remove and Russian environments UI on both VMs. The later retry-notice fix is accepted from installed dc0ddfe packages on both guests. |
| Optional greeter | Complete independent frontend. Installed dc0ddfe packages pass real wrong/correct password, Karyo login and GUI logout on both guests; LightDM restored. Public appearance publication and adoption are also accepted from installed 4726565 packages on both bases. |
| IBM Plex | Original 16-file payload with OFL notices, native hash/family/glyph regressions, native DEBs and installed Sans/Mono fontconfig resolution accepted on both bases. |
| Compositor layout extension / optional integrations | Separate reviewed packages and explicit capability gates; no mandatory VPN/VMware/Wine services in the base DE. |
| OS defaults, browser/boot/installer, ISO generation | Owned by Karyo Linux/Builder stages, outside the DE migration scope. |

Installed application checkpoints and later common fixes are detailed in Builder
VALIDATION-APPLICATION-WAVE.md and VALIDATION-GREETER.md. Earlier per-application
test totals below are historical checkpoints, not the current suite size; the
last accepted complete package wave passes 68/68 on both native bases; later
source changes have separately identified gates above.

## Validation policy and retained evidence

Every migrated component must compile and run applicable regressions in native
Debian 13 and Ubuntu 26.04 environments; do not reuse binaries across native
bases. Use real VM sessions for desktop/PAM/portal behavior. Hardware-dependent
results must state whether they used virtual devices, fixtures or actual hardware.

Local ignored Builder output out/migration contains native build/test logs and
separate package records. out/debian-vm-qa retains the previous clean Debian VM.
A separately provisioned Ubuntu VM under out/ubuntu-vm-qa passed real login,
lock/sleep, polkit/tray/portals, native app checks, upgrade/removal and Xfce
coexistence. Builder docs/VALIDATION-UBUNTU-VM.md records exact scope and limits.
No credential, personal profile, private endpoint or machine image belongs in Git.

Before morning review, list exact commits, verified source revisions, actual
commands/results, package hashes and remaining limitations. Local commit trailers
must contain Co-Authored-By: Codex <noreply@openai.com> exactly once. Do not push
these commits or include another agent's modifications.

## Local review checkpoint after Files

Twelve of fourteen application families now have source/runtime components:
Terminal, Camera, Photos, Calculator, Clock, Editor, Reader, Player, Monitor,
Snip, Archive and Files. Settings and App Center remain, together with the
greeter, compositor and reviewed system integrations listed above. The complete
38-entry CTest suite passes on Debian 13 and Ubuntu 26.04; the Wayland-only Files
focus case is intentionally not accepted by its offscreen run. Archive's full
31-case Qt backend suite additionally passes as UID 65534 on both bases without
skips. Builder host tests are 17/17.

The first 17-package native wave uses exact DE
21fdf2e4e0636d2ef17d0a8a72dce42475762bee. Debian installs 16 packages, leaving
the optional non-free RAR addon uninstalled; Ubuntu installs all 17. Files and
session-entry are subsequently built and installed from exact DE
231bcff29796042ae0a810d6b5a212935af1a893. The corresponding Builder manifests,
input hashes, native base identities and installation logs remain local.

Real new-session checks passed on both test VMs. Calculator, Clock and Monitor
launch and render; Snip screen capture and editor Copy publish 1280x800 PNG,
region capture publishes 401x231 PNG for inclusive pointer endpoints, and Escape
retains the prior clipboard. Archive browses a public Python ZIP and extracts
its UTF-8 files correctly. A Debian extraction failure was a QA-helper mistake:
it inherited the admin's inaccessible cwd. The fixed helper uses the test user's
home, as normal desktop-entry launches already do. This failed attempt is not
counted as product acceptance.

Files runs its idle FileManager1 server and accepts ShowItems on both VMs. A
selected 42-byte QA folder shows two files and one subfolder, with Details open.
Its refusal to replace a running Thunar was also observed before deliberately
closing that QA fixture. FileManager1 folder routing added after the package
wave is natively verified but not yet installed in these VM artifacts.

Installed native apps and their regressions do not establish physical camera,
microphone, audible playback, Bluetooth, Wi-Fi hardware or multi-GPU acceptance.
The test VM has no audio device; actual PCM decoding is independently verified.
The stock compositor still lacks the window-snapshot and keyboard-control
extensions, whose separate package/acceptance work remains in this goal.

No commits have been pushed during this goal. All remaining scope stays active.


## Settings and software-management checkpoint

Thirteen application families now have independent source/runtime components;
App Center is the remaining family. Settings includes display/audio/notification,
storage, Bluetooth/input, network, personalization, defaults/accounts, language/
time/privacy, updates and system information. It retains compatible user
preferences and a complete Russian catalog. Shared software jobs survive page
closure and hold normal application quit until completion; a separate optional
runtime owns the narrow authorized APT helper and polkit action.

Exact DE 93765d7c6cb8fb1cb77d5d89b3f52a0b382ba4e1 produced a native 20-package
set on each base, passing all 53 then-current CTest entries. Nineteen packages
were reinstalled on Debian (optional RAR omitted); Ubuntu installed all twenty.
The dpkg audits are empty and the polkit action resolves to the fixed helper
with administrator authorization. New real Karyo logins after these installations
succeeded on both disposable VMs. No owner VM, laptop or APT source list changed.

The real Settings network page exposed a previously untested condition: Debian
showed loopback while Ubuntu reported disconnected despite SSH connectivity.
Their cloud QA bases use other interface managers. This is a failed network UI
check, not successful acceptance. The new read-only observation fix selects a
real interface and supports networkd/ifupdown without reconfiguring them. Its
parser/radio/window regressions pass on both bases; live fix acceptance and a
new package containing it remain explicit gates.

App Center's external catalog and software/environment model pass native and
unprivileged fixtures on both bases. Miniforge bootstrap testing substitutes
all downloads and installers; no real conda installation is claimed. The model
retains APT, per-user Flatpak and conda behavior with bounded queries, visible
errors, APT serialization and safely quoted environment scripts. The optional
catalog retains 58 reviewed public package descriptions and no private endpoints.

All local commit trailers and substantive body paragraphs were audited through
DE 4d75dab and Builder c356323 (50 and 15 local commits respectively). Nothing
has been pushed. The compositor, greeter, profile/system integration and Plex
packaging/acceptance work remain part of the active goal.


## Complete application source checkpoint

All fourteen application families now have independent source/runtime components.
App Center includes discovery/categories, installed software, catalog/APT/Flatpak
search, details, updates, environment inventory/import/export/create/remove,
terminal activation and explicit checked Miniforge setup. The optional public
catalog remains external data, with no profiles or downloaded environments in
Git. Russian chrome has 98 messages. Model jobs outlive pages; theme rebuild
retains the model, active route and details/search back navigation.

The full native suite now passes all 59 CTest entries on Debian 13 and Ubuntu
26.04. App Center model/pages/window also pass as UID 65534. A full Ubuntu run
exposed a prior intermittent ZipCrypto failure: a wrong password can pass its
one-byte header check and fail at the data CRC. A deterministic synthetic fixture
reproduced the bug on both bases before the fix. Archive now retains password
retry for these encrypted decoder failures and all 32 backend Qt cases pass
as root and UID 65534; damaged unencrypted ZIP does not request a password.

This checkpoint closes source-level application migration, not the whole goal.
The new App Center runtime and latest Settings/Archive revisions still need a
verified native package wave and real VM acceptance. Greeter, optional compositor
extensions, profile/system integrations, actual Plex font packaging and existing-
window Files activation remain separate integration gates. Physical device
acceptance cannot be inferred from these virtual machines. No commits are pushed.


## Font and greetd preparation

The font runtime now carries the original 16 complete IBM Plex Sans/Mono files
with hash records and original SIL OFL notices. Eighteen native Qt cases verify
families and Latin/Cyrillic glyphs on both bases, also as UID 65534; staged
fontconfig resolves the supplied files. No global family aliases are installed.
Native DEB and real VM font acceptance remain pending.

The optional greetd transport is migrated with bounded native-endian JSON framing,
validated reply types and safe teardown when a receiver destroys the client.
Private-socket tests cover fragmented/coalesced messages, null acknowledgements,
literal command/environment fields, malformed/oversized/truncated inputs and
receiver teardown. Fourteen Qt cases pass as root and UID 65534 on both bases.
The complete current native suite passes 61/61. This does not yet establish a
working greeter UI, real greetd authentication or a display-manager replacement.

Shared sign-in presentation is now one internal target, reused by the lock and
prepared for the optional greeter without sharing authentication decisions.
User names/details and authentication messages are plain text. Lock layout,
PAM policy and greetd IPC regressions pass on both bases after relinking; the
view also passes as UID 65534. This does not replace the display manager.
The latest Builder application-wave report closes native-DEB App Center and
Settings network acceptance at exact DE 2e33ed6; later source changes retain
their explicitly separate packaging gates.

The optional greeter now also has a separate authentication controller. Session
selection is captured per attempt; only greetd success authorizes start and its
start acknowledgement announces readiness. Repeated secret/visible/info/error
questions, cancellation acknowledgement, denial/retry and disconnect paths are
covered by a private fake daemon. Pretyped passwords never answer visible
questions, and embedded NUL responses are rejected. Ten controller Qt cases
pass as root and UID 65534 on both bases; the full native suite is now 62/62.
Greeter inventory/view wiring, packaging and real greetd login remain pending.

Greeter discovery/preferences now have native and unprivileged acceptance.
Only main login accounts appear; invalid desktop ownership configuration stops
discovery instead of trusting partial mappings. Installed executable Wayland
sessions coexist, Karyo is preferred, and TryExec/DesktopNames use the shared
Desktop Entry parser. New preferences are bounded atomic JSON; a malformed
primary file cannot revive legacy selection. Six inventory Qt cases pass as
root and UID 65534 on both bases, and the full native suite passes 63/63.

Greeter appearance adoption now validates a single no-follow file descriptor's
owner/type/size, reads at most 4097 bytes and accepts only public theme/accent
values. A bad higher-priority publication cannot revive a legacy file. Five
appearance Qt cases pass as root and UID 65534 on both bases, including symlink,
FIFO, oversized, wrong-owner and invalid-value paths. Publication by the later
system integration and real greeter rendering remain separate acceptance gates.

Greeter UI wiring now exercises real shared widgets against a private daemon:
user/session selection, frozen choices during authentication, visible and secret
prompts, cancellation reset and post-start-only preference publication. A lost
connection before controller construction also fails explicitly. This remains
source-level coverage; the runtime executable and real greetd login are separate.
All 65 native CTest entries pass on Debian 13 and Ubuntu 26.04. The greeter
window workflows also pass as UID 65534 on each base.

The optional `karyo-greeter` runtime and embedded Russian catalog now build on
both bases, with a NoDisplay desktop identity for Qt portal lookup. Its component
stages only the executable, desktop identity and documentation/example; it does
not install active display-manager configuration or PAM policy. All 66 native
CTest entries pass on Debian and Ubuntu. Real greetd login remains pending.

A real Ubuntu Wayland trace reproduced the terminal close-button failure: a
compositor-repositioned tooltip popup received the press instead of the button.
Widget-level mouse transparency was insufficient. Tooltips now also declare
native WindowTransparentForInput. The rebuilt terminal closes on the first real
VNC click on both Debian and Ubuntu; the process exits and Wayland objects tear
down normally. Existing shared-widget and terminal tests pass on both bases.
This check uses a named QA overlay binary, not an installed package revision.

The first real greetd run exposed a cage/wlroots initialization assertion on
Ubuntu: the client's initial fullscreen request preceded xdg_surface setup.
Letting the kiosk compositor size a normal shown window renders correctly on
Ubuntu and Debian. Fatal UI reasons now reach stderr and clear unsubmitted input;
a native disconnect regression verifies the signal contains only the reason.
The real wrong-password path still exposed a separate cleanup/retry issue and
is not accepted yet. Package installation itself preserves LightDM, and both
installed font runtimes resolve the original Plex Sans/Mono files with fc-match.

A real wrong-password attempt revealed greetd 0.10's terminated-PAM-worker
cleanup error. The client now retries cancellation at most once after an
explicit auth_error and still requires a positive cancellation acknowledgement
before a fresh attempt. Further failure is terminal; authentication/session start
is never inferred from a cleanup response. Native regressions reproduce failed
worker cleanup, prevent early reuse and prove the retry is bounded.

On both disposable VMs the revised QA frontend now passes real wrong-password
recovery, correct password, Karyo session launch and GUI Sign out returning to a
fresh greeter. Debian uses English/light; Ubuntu proves Russian/dark-sage adoption
from a correctly owned public fixture. Both VMs were restored to their original
LightDM and have clean dpkg verification/audit. This frontend is a named overlay;
the installed 90df39b package wave precedes the kiosk and cleanup fixes. A final
native package refresh remains required. No owner VM or physical laptop changed.

The shell now publishes only its theme/accent at startup and after desktop
configuration changes. Its effective account is resolved through passwd, not
USER. Writes are bounded and atomic relative to a no-follow directory descriptor;
symlinks, special files, another owner's publication and non-sticky writable
shared directories are refused. The optional greeter supplies only a tmpfiles
data-directory declaration; missing publication infrastructure never prevents
starting the base session. A real UID 65534 test first reproduced the unlistable
1733 directory failure with O_RDONLY; O_PATH fixes it on both bases. All eight
reported publisher Qt cases pass as root and UID 65534, including writer/reader
interoperability and a root-owned foreign publication that must stay unchanged.
Real installed-session publication still needs its own runtime package refresh.
The full current suite passes 67/67 on each native base. Isolated tmpfiles
processing creates a root-owned 1733 directory, and the publisher also builds
and passes without enabling the optional greeter.


The corrected dc0ddfe native 24-component wave is now installed and accepted on
both disposable guests. Actual /usr/bin greeters pass denial/retry, successful
session start and GUI Sign out returning to a new greeter. Installed terminal
close and App Center cancellation/retry-notice behavior also pass on both bases.
Original LightDM and display-manager symlinks were restored; package verification
and dpkg audits are clean. This supersedes the prior QA-overlay-only gate for
those fixes. Later appearance publication remains distinct until its new shell/
greeter packages are installed and exercised.


The installed shell/greeter pair from 4726565 now passes the remaining public
appearance integration: ordinary LightDM login publishes light/terracotta with
UID1001/mode0644 in a root-owned 1733 directory; actual Settings changes publish
dark/moss (Debian) and dark/sage (Ubuntu). The installed greeter adopts those
exact publications in a fresh real greetd session. No manual publication fixture
is used in this scenario. LightDM was restored and package verification/audits
remain clean on both disposable guests. This supersedes the pending runtime
gate for appearance publication; compositor/profile/Files gates remain open.

## Native layout and window snapshot protocols accepted

The earlier stock-compositor limitation is now closed for these two protocols.
The optional integration in integrations/compositor rebuilds each base's native
source, preserves its wlroots ABI and enables upstream test suites. Exact DSC
parts and patches are checksummed; generic isolated source packaging lives in
Builder. Ubuntu uses its native descriptor/view-title API instead of old 0.8 code.

Both final DEBs passed real headless server/client acceptance as UID 65534:
registry discovery, mapped-window order, minimized exclusion, security-context
filtering, locked-session privacy/layout denial and unlock recovery. Installed
Debian and Ubuntu QA VMs additionally verified actual keyboard group events,
real shell RU-to-EN clicks, and Snipping Tool window selection into the editor.
Stock rollback and downstream reinstallation passed without changing wlroots or
LightDM. Fixture keyboard files were restored and sessions ended afterward.

This closes only layout/snapshot extensions. Prototype Wine/VMware compositor
changes, profile/account/PAM/network/DNS/shared-clipboard helpers, Files existing-
window activation and hardware-specific acceptance remain open.

## Files background activation source fix

A separate GUI fixture reproduced the actual remaining failure on both installed
QA sessions: clicking Open folder for an existing Files tab left the requester in
front. The shared helper had always sent an empty startup ID. It now forwards a
standard per-request Wayland activation token from the focused caller, using the
real input serial and surface without bypassing compositor policy.

Native fixture before/after checks pass against installed Files on Debian and
Ubuntu. A private-bus real-Wayland fixture also verifies distinct concurrent
tokens and cancellation when the owner is destroyed, with no late duplicate.
All 67 CTests pass on both bases. This closes the source/real-client regression;
new native caller packages and their installed application acceptance follow.

## Installed background Files activation gate closed

Both eight-runtime native package sets from exact source
82f9d4b0de338cf8339eb539aa2232c25055b495 passed all 67 CTests, verified artifact
hashes, installation and dpkg integrity. Installed /usr/bin/karyo-photos menu
Show in Files raised the already-open target folder and selected photo.png on
both native VMware sessions without another Files window. The same Files PIDs
survived package installation. Original LightDM remained active after ending
fixture sessions. Builder docs/VALIDATION-FILEMANAGER-ACTIVATION.md retains exact
hashes and before/after evidence. This closes the source-and-installed activation
gate; optional profile/PAM/network helpers and other compositor patches remain.


Shell output recovery now passes native source-overlay acceptance on both QA
VMs. Two real VT roundtrips, including retirement of an open Start flyout, retain
shell and Terminal PIDs, visible returned panels, working Start and DND state.
The shell skips Qt placeholder outputs and recreates only output-bound views;
shared models and DBus services persist. Exact clean native shell packages still
need installation acceptance before closing the installed gate.


The clean native shell DEBs built from bb46dfc now also pass installed output
recovery on Debian and Ubuntu: all 68 CTests pass, no source overlay is present,
and actual initial PAM followed by VT return restores the panel and Start.
A Start flyout is open when leaving; shared DND and the shell/Terminal PIDs
survive. Input hashes, GUI captures and unchanged account/PAM/package checks
are recorded in Builder VALIDATION-INSTALLED-SHELL-HOTPLUG.md. This closes the
reproduced installed shell output-lifecycle regression for one virtual display;
physical multi-monitor and actual two-profile switching remain separate gates.


## Generic Win32 owned-window compositor slice

The native labwc karyo3 recipes now include generic hidden-owner palette grouping
and scene-only minimize/restore. Native Debian/Ubuntu headless XWayland checks
reproduce the missing behavior on karyo2, pass on karyo3, retain four excluded
layout behaviors and exercise four late palettes. Normal and initial-lock
Wayland protocol gates still pass. There are no RomArtist rules or prefix edits.
This is one remaining compatibility capability, not acceptance of the complete
Wine runtime: actual Win32 binaries, focus/menu fixes, captions, launchers and
installed graphical upgrade remain open. See integrations/compositor/README.md
and Builder's owned-window validation report for provenance and exact evidence.
