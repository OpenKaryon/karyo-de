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
held until the compositor confirms the managed lock; real suspend/resume
still needs acceptance in a systemd/logind machine.

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

Ten CTest targets cover foundation, icons, widgets, style, launcher, PAM
policy, tray, Start filters and shell-service policies. Independent installed
SDK consumers continue to verify the UI component boundary.

`tests/wayland-session-smoke.sh` exercises an actual headless labwc session
under an unprivileged disposable fixture account: startup, Start, confirmed
Wayland lock, a wrong password, a correct PAM password and clean logout.
It expects KARYO_TEST_PASSWORD, XDG_RUNTIME_DIR and KARYO_TEST_ARTIFACTS.
KARYO_TEST_PIPEWIRE=1 additionally starts the fixture PipeWire daemon and checks
the ScreenCast portal interface before the lock scenario.
Use only disposable accounts, never a real user's password. The test is
separate from CTest because it requires installed packages and a PAM fixture.

Clean package installation/removal checks preserve /etc/os-release, another
session entry and display-manager configuration. A complete display-manager
login cycle, hardware suspend/resume, multiple physical outputs and GPU paths
remain VM/hardware acceptance gates. Headless results do not establish them.
