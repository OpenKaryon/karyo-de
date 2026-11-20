# Standalone session preparation

The launcher and labwc configuration establish the standalone session boundary.
The public shell and lock executables are subsequent migration work. This
preparation does not yet establish a complete selectable desktop on Debian.

The preparatory package is karyo-session-entry. A future complete DE metapackage
will also require the migrated shell and lock executables.

The launcher contract requires `labwc`, `karyo-shell` and `karyo-lock`.
`karyo-session --check` reports missing programs before starting the compositor.
The Wayland session entry uses TryExec=karyo-shell so an unfinished desktop
does not appear as an available session. The install component is
`session-runtime`; it includes only the launcher, session entry, configuration
and documentation.

The launcher sets Karyo's desktop identity and Qt style for its own session,
preserves other environment choices, and uses a session bus or dbus-run-session.
It writes a per-user state log and returns the compositor's exit status.
The keyboard/renderer helpers retain the prototype's configured layouts,
Alt+Shift fallback and safe rendering behavior. Explicit WLR_RENDERER choices
are preserved. User environment settings prefer karyo/session.env and retain
the previous openkaryon/session.env path as a fallback.
The login manager, host OS identity, browser defaults and existing session
entries are outside this package's ownership. No maintainer scripts modify them.

The compositor autostart updates the activation environment after Wayland is
ready and retains the prototype's bounded shell restart behavior.
The installed style plugin supplies Qt controls; platform themes/portals remain
separate. Native window rules recognize both Karyo and prototype application IDs.

## Remaining session gates

Migrate shell/notification/tray/polkit and their file-operation/catalog
dependencies, plus the PAM/Wayland lock. Separate the optional greetd frontend
from the required lock. Review compatible labwc/wlroots versions and the portal
provider. Package optional desktop isolation, VMware and compatibility runtimes
independently.

Only after those components exist should clean Debian login, lock/suspend,
session selection, coexistence, upgrade and removal be accepted. Current tests
use isolated command fixtures to validate the launcher contract and return
status. They make no claim of a complete desktop login or hardware validation.
