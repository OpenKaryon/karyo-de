# Standalone Karyo session

The public shell and PAM/Wayland lock now run independently of the prototype.
`karyo-session` starts labwc, with the shell on its background and panel layers.
`karyo-shell --start` toggles Start through the session bus. `karyo-lock` uses
ext-session-lock-v1; it does not emulate a lock with an ordinary fullscreen
window. PAM checks the signed-in user using /etc/pam.d/karyo-lock.

## Package boundaries

Builder provides karyo-shell, karyo-lock, karyo-session-entry and the
karyo-session bundle. Install the bundle to obtain the complete session.
The entry package remains a small launcher/configuration component and keeps
its original name for development compatibility. The Qt style has its own
runtime package; SDKs are not required to run the desktop.

The Wayland entry is /usr/share/wayland-sessions/karyo.desktop. Its
TryExec=karyo-shell hides it when the shell is absent. The launcher checks
labwc, karyo-shell, karyo-lock and its configuration before starting.
Select Karyo in an existing display manager, or run karyo-session from a
normal logind login on a dedicated VT with XDG_RUNTIME_DIR available.
The optional greetd greeter is not installed and the login manager is not
replaced. The launcher returns the compositor's exit status.

The default portal choice is installed as a Karyo-specific data file,
karyo-portals.conf: GTK handles ordinary dialogs and wlr handles screen
capture/casting. Other desktops retain their own portal selection, and user
configuration takes precedence. Format reference:
https://flatpak.github.io/xdg-desktop-portal/docs/portals.conf.html

The lock package owns only its PAM policy; Debian conffile metadata preserves
administrator edits on upgrade. It is an ordinary unprivileged application,
using PAM's distribution-provided password helper. No setuid executable or
global authentication bypass is introduced by Karyo. Logind sleep delay is
held until the compositor confirms the managed lock. A clean Debian VM has
now exercised actual logind suspend/resume, retaining the lock and application
processes after a virtual hardware-key wake. Physical devices remain a separate
acceptance gate.

## Retained behavior and optional integrations

The shell contains wallpaper/desktop file operations, taskbar, Start search,
status-notifier tray, notifications, quick settings and a session polkit agent.
Freedesktop autostart entries run once per compositor session through dex.
An existing notification daemon or polkit agent remains authoritative if it
already owns the standard service. Settings, audio, Bluetooth, power and network
controls use available host services instead of installing private OS helpers.
Settings buttons are disabled when no compatible settings application exists.

The entry adds its packaged configuration directory only to Karyo's XDG view.
It masks MATE's broadly enabled authentication-agent autostart entry, which
otherwise races Karyo Shell for the same session. The host MATE launcher and
its behavior in other desktops remain intact; user autostart preferences retain
their normal higher priority. The configuration scope is also imported for
ordinary session-bus activation.

Per-user openkaryon/desktop.conf and shell.conf remain compatible with the
prototype. User environment prefers karyo/session.env, with the old
openkaryon/session.env as a fallback. Renderer and keyboard helpers preserve
system settings, explicit WLR_RENDERER and the multiple-layout Alt+Shift
fallback. The public desktop identity and Qt style apply only to this session.

The command-line app catalog is optional XDG data at karyo/catalog.json; no
App Center source or private catalog is embedded. Isolated user desktops are
an optional external integration; their panel control is hidden without its
helper. VMware, VPN namespaces, compatibility runtimes and OS/browser/boot
policies remain separate products.

The existing custom keyboard-layout wire protocol remains compatible with the
patched compositor. With stock labwc, the indicator can observe physical
layout changes but its cycle button is disabled when that extension is absent.
Packaging the reviewed compositor extension is a later compatibility task.

## Validation and remaining acceptance

Eleven CTest targets cover foundation, icons, widgets, style, launcher, PAM
policy, password error layout, tray, Start filters and shell-service policies. Independent installed
SDK consumers continue to verify the UI component boundary.

`tests/wayland-session-smoke.sh` exercises an actual headless labwc session
under an unprivileged disposable fixture account: startup, Start, confirmed
Wayland lock, a wrong password, a correct PAM password and clean logout.
It expects KARYO_TEST_PASSWORD, XDG_RUNTIME_DIR and KARYO_TEST_ARTIFACTS.
KARYO_TEST_PIPEWIRE=1 additionally starts the fixture PipeWire daemon and checks
the ScreenCast portal interface before the lock scenario.
Use only disposable accounts, never a real user's password. The test is
separate from CTest because it requires installed packages and a PAM fixture.

A fresh Debian 13 VMware guest additionally passed real LightDM login/logout
and a second Karyo login alongside Xfce. The graphical checks exercised actual
PAM lock/rejection/unlock, logind s2idle/resume, Start-launched polkit authorization,
Qt tray activation/context menus, FileChooser responses and a live 1280x800
Chromium/PipeWire monitor stream. This found a generic desktop-launch bug:
startDetached orphaned pkexec, which refused a dead parent. Launches now keep a
live direct caller while allowing ordinary apps to outlive their launcher.

Installing/upgrading/removing the bundle preserved /etc/os-release, LightDM and
Xfce's session entry. Existing application PIDs survived install/upgrade; Xfce
remained usable and logged in again after removal. The local lock PAM file
survived a changed-default upgrade. VM artifacts and fixture provenance are
recorded in Karyo Builder's docs/VALIDATION-DEBIAN-VM.md.

The VM used software rendering and one virtual monitor. Physical suspend/wake
devices, multiple physical outputs and accelerated GPU paths remain hardware
acceptance gates; the VM result does not establish them.


Shell and lock runtime components include hidden desktop descriptors for their
public application IDs. Qt 6.10's portal registry requires these to resolve the
calling process. NoDisplay keeps service processes out of the normal app list;
this is not an autostart registration. Ubuntu VM validation confirms the IDs
resolve through GIO and the missing-AppInfo portal registration error disappears.

### Optional Files ownership

When `karyo-files` is installed, Karyo autostart launches its headless
`--dbus-service` mode to provide FileManager1 on demand. It opens no Home window
and remains available when the last folder window closes. An already-owned
FileManager1 causes that server to exit; it never replaces another manager.
The package installs only its compatible private D-Bus activation identity,
not a generic FileManager1 activation override in existing desktop sessions.
User/default directory MIME associations remain an OS or user choice.

### Optional compositor extensions

The standalone session works with native labwc. The additional physical layout
indicator control and screenshot window picker require the reviewed native
protocol packages described in [compositor integration](../integrations/compositor/README.md).
Separate patches follow Debian 13 labwc 0.8.3/wlroots 0.18 and Ubuntu 26.04
labwc 0.9.3/wlroots 0.19. Both have real headless and installed-VM acceptance;
the package changes neither the display manager nor the native wlroots library.
Other prototype compositor and profile integrations are not implied by this slice.

## Mandatory protection in the running shell

The shell reads root-owned desktop settings for its real process UID when its
logind session becomes inactive. PASSWORD=home/separate requires the PAM lock;
invalid settings also request a lock. USER, LOGNAME and the legacy password
session environment variable cannot downgrade that policy. The optional user
lockOnLeave preference can add a lock to an unprotected desktop.

This deactivation reaction is defense in depth. It does not prove safe entry:
the privileged switch controller still needs to confirm the target compositor
lock before activation, and initial protected sessions need a startup gate.

## Initial protected-session authentication

karyo-lock --entry-check reads root settings for the real UID before a compositor
starts (0 unprotected, 3 protected, 1 invalid). Protected sessions require the
optional native labwc --startup-lock capability. Missing support or invalid policy
stops startup; there is no silent stock-backend fallback. User session.env may
select keyboard/renderer preferences but cannot override the root password mode.

The compositor starts locked before outputs/input/normal clients become usable.
Its autostart first runs karyo-lock --on-start synchronously. An unprotected user
returns immediately without a Qt display; a protected user must complete actual
PAM authentication. Shell, FileManager1, desktop autostart and activation exports
start only after success. Locker error/refusal/crash exits the compositor. A root
administrator changing policy during startup remains trusted; an ordinary user
cannot alter those files. Existing-session switching still needs a privileged
lock-before-activate controller and its separate installed acceptance.

## Lock output lifetime

The compositor lock is acquired through a hidden connection window, independently
of which monitors exist. A temporary Qt placeholder has no wl_output and is never
given a lock-surface role. Each real output gets an interactive password form;
removed forms clear their text and are retired, and replacement outputs can
accept the password without keeping an obsolete primary screen alive.

One shared PAM request disables every form, including a form added while that
request is pending. Removing a screen does not cancel PAM or release the lock.
Errors update all surviving forms; successful PAM releases the original lock
through its stable display connection, even if earlier windows disappeared.
Queued role creation rechecks the guarded screen/output instead of capturing a
possibly destroyed wl_output. No application-specific rule is involved.

Native Qt 6.8 and 6.10 builds pass the lock/account/session regressions. Separate
QA source overlays reproduce actual VT transitions on both VMware guests. The
previous Ubuntu wallpaper-only failure is corrected: the replacement screen has
a password form, wrong passwords remain rejected, and correct passwords return
to the same running shell. A second Ubuntu output replacement during a pending
wrong-password PAM request also retains an interactive error/retry form. The
source overlay is removed afterward; final installed-package acceptance remains
separate until a new lock package is built and tested.

## Installed lock output-lifecycle acceptance

Final lock DEBs from 283cce4 pass clean native 68/68 CTests and actual installed
initial PAM/VT-return checks on both QA guests without any source overlay. The
fixed-host installed-startup.py roundtrip-vt action refuses an occupied console,
waits for lock readiness while inactive and always restores the graphical VT.
It prints READY rather than accepting an invisible password form. Visual forms,
wrong-password errors and correct-password unlock are checked separately; a
second Ubuntu output replacement during a pending PAM failure also passes.

This closes the lock/PAM output-lifecycle gate, not all shell output lifetime.
The same full-session inspection reveals a distinct Ubuntu panel defect after
output replacement: the shell is alive and can open its menu and applications,
but the existing taskbar is invisible. That shell surface lifecycle is a separate
open regression; it must be fixed before claiming complete profile VT switching.


## Shell output lifetime

The shell keeps its application index, tray host, network model, notification
server, polkit agent and session locker alive while outputs change. It tracks
wallpapers by real Wayland output and recreates the primary-output taskbar and
desktop icons after removal or primary-screen replacement. Qt placeholder
screens never receive shell layer roles. The DBus service remains available
without a taskbar and safely ignores Start until a real output returns.

Retired panels hide and destroy their output-bound flyouts and notification
banner. The add-desktop form uses an asynchronously owned dialog, so output
removal cannot delete a stack dialog inside a nested event loop. New layer
windows select their screen before their platform window is created.

Native source-overlay QA on Debian 13 (Qt 6.8) and Ubuntu 26.04 (Qt 6.10) now
passes two actual VT roundtrips, including one with Start open. The password form
remains interactive, the returned taskbar and Start are visible, shell and
Terminal PIDs do not change, and the notification service retains its DND state.
This fixes the reproduced Ubuntu disappearing-panel case in the tested source
builds. Clean native DEB installation acceptance and physical multi-monitor
coverage are separate gates; this is not yet full two-profile switching.


The clean native shell DEBs built from bb46dfc now also pass installed output
recovery on Debian and Ubuntu: all 68 CTests pass, no source overlay is present,
and actual initial PAM followed by VT return restores the panel and Start.
A Start flyout is open when leaving; shared DND and the shell/Terminal PIDs
survive. Input hashes, GUI captures and unchanged account/PAM/package checks
are recorded in Builder VALIDATION-INSTALLED-SHELL-HOTPLUG.md. This closes the
reproduced installed shell output-lifecycle regression for one virtual display;
physical multi-monitor and actual two-profile switching remain separate gates.


## Public profile menu

The optional desktop button now uses karyo-desktops, without a private prototype
binary. A session-owned ProfileControl runs bounded asynchronous commands,
coalesces list requests and serializes mutations. It survives output-bound
panel/dialog replacement; stdin-only separate passwords are cleared after
handoff, and responses/diagnostics have a combined 256 KiB bound. List calls
have an eight-second deadline; mutations have a three-minute watchdog. An
interrupted or malformed mutation is reported as uncertain instead of safe to
repeat. Retained-account errors likewise disable retry in the current form.

The menu shows loading/error states, current/running profiles and the creation
form. Switching and creation report failures without blocking the shell event
loop. Root helpers remain the authority for family, local caller and password
policy. The existing-only backend reports stopped profiles visibly; owned
startup and per-profile networking are still required rather than silently
launching an unisolated child.

Native source-overlay QA on Debian and Ubuntu passes list/current display,
empty-name rejection, real unprotected child creation from the form, refreshed
list, visible stopped-target error and actual running-profile menu roundtrips.
The application-owned job survives panel retirement and both shell PIDs remain
unchanged. All 17 controller cases pass as ordinary UID on both bases, covering
event-loop progress, input/argument separation, consumer removal, double starts,
malformed/wrong/oversized responses, result classes and timeouts. Four targeted
CTest regressions pass per base. Final native package acceptance remains separate.


The final clean shell packages from 2a77771 now also pass installed public-menu
acceptance on both guests, with all 69 CTests passing and no source overlay.
The real form creates a shared child through native pkexec. Protection is then
enabled before its first native LightDM session starts; the initial password
form precedes shell/Files. Actual menu clicks lock the protected source on leave
and demand fresh Home authentication on re-entry, with both shell PIDs preserved.
QA cleanup restores original account/common-PAM bytes and package verification
is clean. Builder VALIDATION-INSTALLED-PROFILE-MENU.md records exact inputs and
limits. LightDM bootstrap is not the pending owned stopped-session launcher.

The optional root profiles library now also provides an owned native namespace
and point-to-point veth primitive. Native Debian/Ubuntu packages from b56892a
pass resource creation, collision, live-consumer/foreign-alias refusal and exact
host-network restoration. This is not yet consumed by a stopped-profile starter.
Private DNS, physical-underlay routing/NAT, user-manager scope, persistent
supervision and complete owned session teardown remain required. See profiles/
README.md and Builder VALIDATION-PROFILE-NETWORKS.md for tested limits.

The next network layer, native physical IPv4 forwarding, is accepted separately:
source/destination PBR, per-profile exclusive nft NAT/filter table and unreachable
fallback pass three fresh two-profile UDP/TCP cycles on both native QA bases
(source 088d333). Original global/per-interface network settings and running GUI
sessions are restored. This is a library acceptance gate, not an enabled session
network service. Private resolver mounts, runtime dependency/forwarding setup,
user-manager scope, NetBird integration, persistence/reconciliation and stopped
session startup/owned teardown remain required. Builder's forwarding validation
report preserves failed native attempts and the exact final package identities.

## Installed private profile DNS

The private resolver library and fixed-host installed-dns.py probe now pass
native Debian 13 and Ubuntu 26.04 with installed source 8b09128. A real ordinary
client retains host logind access and normal sudo while resolving through its
own private resolved instance. Read access is allowed; private-bus mutation is
denied to that client. Root bootstrap validates private resolver name ownership.

The same client survives global host stub atomic replacement and retargeting
between the two canonical resolved aliases, then sees changed private DNS after
NetBird-style per-link updates. Original host file/symlink identities and network
snapshots are restored; both existing graphical shells keep their PIDs. Builder
VALIDATION-PROFILE-DNS.md records native inputs, failed attempts and limitations.
This closes a resolver-library gate, not stopped-profile/session supervision or
actual NetBird integration. Unsupported resolver layouts/transports are refused.

## Owned private resolver controller acceptance

ResolverServices now consumes the private network/configuration leases and owns
the native bus/resolver units by InvocationID plus pinned process identities.
It proves the private bus owner before bootstrap, rejects existing/stale unit
identities and refuses shutdown while namespace clients remain. It requires the
host user/network/mount context even after a profile client elevates with sudo.

Clean packages from b08ce4d pass the extended installed DNS gate on Debian and
Ubuntu. Duplicate/stale leases leave actual services running; the same client
continues DNS through atomic host/private resolver changes. No service is
enabled automatically. Root runtime persistence/recovery, actual stopped-profile
startup/teardown and user-manager/network/VPN orchestration remain open.

## Installed offline user-manager scope

The native PAM experiment confirms that a session process belongs to a separate
session.scope while its user manager belongs to user@UID/init.scope. Complete
logind ownership includes manager, remote, TTY and closing sessions. The new
scope uses those native identities, a fresh drop-in and actual resolver mount
identity rather than treating a missing GUI session as an offline account.

Installed packages from db70d67 pass Debian/Ubuntu acceptance: reserved VT9 stays
inactive, manager and activated child share private network/DNS, ordinary
password sudo and host logind work, and the child has zero inherited/ambient
capabilities. The first native run reveals PAM's residual CAP_WAKE_ALARM in a
manager job's inheritable set; a verified default ExecStart wrapper fixes it
without reducing the bounding set or adding NoNewPrivileges. Custom manager
commands are refused. Native LoadUnit permits typed inspection of idle units
without starting them.

Normal logind shutdown releases the captured manager in about ten seconds.
Accounts, PAM, network and main GUI state are restored. This is a real
user-manager/PAM scope acceptance gate using a sleep process, not yet the
controlled graphical starter, startup authentication or complete session
supervisor. Builder VALIDATION-PROFILE-USER-MANAGER.md records exact evidence.

## Installed controlled graphical profile entry

The optional PAM/session-entry component and GraphicalSession library now pass
real installed Debian/Ubuntu QA with source f2edff3. A new native PAM login stays
on inactive reserved VT9 until the root controller confirms its active source
and enters it. The post-PAM entry clears inheritable/ambient capabilities before
waiting, then executes the ordinary karyo-session entry with its protected
compositor startup gate. Its process and logind scope identities are independent
of the original system unit cgroup.

Both native bases pass real unprotected, Home-password and separate-password
entry/return cycles. Wrong passwords retain the initial lock and prevent shell/
Files startup; correct authentication starts them, and protected re-entry asks
again without changing shell PIDs. Explicit owned teardown and normal logind
manager shutdown restore original account/PAM/network and main session state.
Published installed-graphical.py accepts bounded QA stdin without credentials
in source. Builder VALIDATION-INSTALLED-GRAPHICAL-PROFILES.md records the exact
packages, screenshots, complete-inventory churn fix and fixed QA recovery.

This closes the controlled graphical primitive, not the full stopped-profile
menu flow. A persistent root host controller must own the graph across commands,
activate forwarding/dependencies, handle failure/recovery and user logout, and
expose the authenticated public operation before that full feature is complete.
Actual private VPN/client integration and other remaining system features stay
separate.


The session launch/data component also configures independently with
cmake -S session -B build-session and retains the session-runtime component
name. This does not build or link the unrelated Qt application graph. Optional
profile application visibility is prepared after initial PAM and before
session clients; without the optional helper/policy, existing startup behavior
is preserved. The shared implementation remains cmake/Session.cmake for both
standalone and full-project builds.
# MIME associations alongside other desktops

Karyo's launchers remain visible in the system application menu, but their MIME
recommendations are scoped to Karyo. The session prepends
`/usr/share/karyo/associations` to `XDG_DATA_DIRS` (using the configured installation
prefix). That directory contains the full desktop entries and an unprefixed
`applications/mimeapps.list` with defaults and Added Associations. Global launchers omit their
MimeType line, so merely installing Karyo does not change another desktop's
implicit choice of file manager or editor. Explicit user defaults still win,
including choosing Karyo Files while using another desktop.

Application visibility catalogs can prepend their own data directories after
the startup lock; their filtered entries keep priority over these recommendations.
The activation environment receives the same data-directory view as the session.
This does not edit the user's mimeapps.list or system-wide Xfce preferences.
