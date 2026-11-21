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
