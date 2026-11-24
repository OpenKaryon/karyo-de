# Optional VMware session ownership

This independent ordinary-user agent runs a foreground native vmusr and the
separate Karyo X11/Wayland clipboard adapter only while its real local logind
session is active and unlocked. Identity comes from the kernel process session,
not XDG_SESSION_ID/USER. Locked/background sessions stop their captured groups.
No session identity is treated as active when missing or ambiguous.

The native open-vm-tools-desktop package supplies vmusr. The initial agent uses
it without the SUID wrapper; current/ambient/inheritable capabilities are dropped
and NoNewPrivileges prevents new privilege gain. A root-owned0644 runtime file
coordinates the channel with flock across cooperating ordinary-user agents. Worker children retain
the reservation and parent-death notification. Dead group leaders stay unreaped
until captured group shutdown, preventing PID/group reuse during cleanup.

Package installation creates no user, modifies no PAM/display manager and starts
no daemon. The tmpfiles declaration provides only /run/karyo-vmware/channel. For
an already running system an administrator can create that metadata with
systemd-tmpfiles --create /usr/lib/tmpfiles.d/karyo-vmware.conf.

The Karyo session masks only its own stock vmware-user autostart using an XDG
configuration directory, leaving the vendor file and other desktops unchanged.
An already-running external vmusr is respected and never killed. The agent waits
for its channel rather than interfering with another desktop's tool. Package
removal restores ordinary Karyo autostart resolution for subsequent sessions.

This is the session/channel component of a larger VMware integration migration.
Rootless clipboard, actual host exchange and native Home/Work handover require
installed acceptance. Privileged vmblock/uinput file transfer, host drag/drop,
HGFS and other-DE handover remain separate integration gates; installed display
following is a separate acceptance gate for the new component below.
It does not claim that rootless vmusr replaces the SUID wrapper for every tool.

Source validation passes11 agent cases (including real inherited-lock worker,
exited-leader group cleanup and parent-death tests) and14 session-launch cases
on Debian13/Ubuntu26.04. Full DE builds pass71 CTest groups on each base. Native
logind LockedHint D-Bus ABI has also been checked on the disposable Debian host.
Actual native vmusr handover and VMware host payload exchange are not established
by these source tests; they remain the next installed integration gate.

If the system bus disconnects, the agent releases foreground ownership and
reopens its native bus connection before a later unlocked read can admit it
again. A focused reconnect regression covers that conservative recovery path.

The fixed-host tests/installed-handover.py gate now also accepts the installed
coordinator/session packages on both native QA guests. Home starts its agent
from a real GUI Terminal in the original logind scope; Work starts cold through
the public profile runtime and actual session autostart. Both retain their real
UIDs, empty current capability sets, NoNewPrivileges and inherited channel FDs.
Switches leave one vmusr and release background workers without replacing the
coordinators. A killed vmusr recovers its sibling pair; killing the Home parent
clears its workers/reservation and the existing Work can reclaim it.

Captured Work, Home agent, QA Terminal and channel metadata are removed afterward;
original Home/account/PAM/display-manager and root vmtoolsd are retained. The
vendor desktop package remains installed, including its enabled vmblock mount.
The QA VMs disable copy/paste/HGFS in their VMX configuration, so this gate proves
native coordinator/reservation/process handover, not host clipboard payload,
privileged file transfer, actual GUI lock/unlock or another DE's channel handover.
See Builder's validation report for exact executed actor versions and the first
Debian Terminal's separately verified cleanup.

The separate tests/installed-lock.py gate now also accepts actual initial and
manual PAM/Wayland lock/unlock together with the installed coordinator on both
native bases. Only a fresh temporary Work credential is changed; its original
inherited hash/policy is restored before captured account teardown. Correct and
incorrect passwords travel through driver memory/stdin and VM-only RFB input,
never command arguments, environment, disk or exported logs.

Initial authentication suppresses the coordinator and every worker. A real GUI
lock later retains the coordinator while stopping its worker pair and releasing
the channel; a wrong password keeps it closed. Correct unlock restores new
workers in the same original Work shell/session. Bounded exclusive-lock checks
wait for completed cleanup rather than equating absent command names with an
already released reservation. Original Home and account/PAM/package state survive.
See Builder's joined-lock report for exact actor versions, retained first Ubuntu
observation race and the distinct host-payload/file-transfer boundaries.

The optional display worker now follows preferred sizes of connected vmwgfx
outputs. It reads the shared kernel connector metadata twice per second instead
of requiring a global udev netlink subscription in private desktop networks.
Changes debounce for half a second, with one later correction; unchanged kernel
metadata does not repeatedly override a user's manually chosen resolution.
Disabled, unknown, non-VMware and ambiguous outputs are left alone.

The worker lives in the coordinator's foreground group set and retains the same
channel reservation. Background/locked sessions stop it with the clipboard pair.
Its bounded wlr-randr requests have their own parent-death guard and inherited
reservation; a blocked request cannot block the coordinator's active-state loop.
The real native session is checked before querying and again before each write.
Failures retry after five seconds without repeated identical error messages.
KARYO_VMWARE_DISPLAY_SYNC=0 opts out; the old OPENKARYON_VMWARE_DISPLAY_SYNC=0
is retained as a fallback when the new setting is absent. The package requires
wlr-randr. Source tests pass 25 cases on Debian and Ubuntu, including real worker
cleanup under UID1001; native resize acceptance is not claimed by these tests.

The fixed-host tests/installed-display.py gate now also accepts the installed
worker in both Debian and Ubuntu Work sessions. It checks native session/UID,
empty current capabilities, NoNewPrivileges and inherited channel ownership;
manual custom mode remains unchanged until activation, background ownership
ends, and reactivation brings the preferred kernel size back in the same Work
session. Debian's older wlr-randr omits refresh rate for a custom current mode;
that native-discovered format is covered by an additional source regression.

The gate restores the original Home geometry/session and removes only captured
Work/channel resources. This accepts activation-time following against real
kernel metadata and compositor state, not an actual host window resize, monitor
hotplug or a multi-monitor layout. Synthetic DRM topology injection did not
provide a reliable host event here and is deliberately excluded from acceptance.
See Builder's VALIDATION-VMWARE-DISPLAY.md for the preserved failed probes,
strict recovery of the separate networking observation and final evidence.

## Stalled transfer diagnostics and shutdown

The coordinator samples only vmusr processes in its captured foreground group,
including their kernel start identity. A sustained uninterruptible wait is
reported once after twenty seconds, with the wait channel when readable.
Recovery is reported separately; background release resets the observation.
Messages also reach the journal as `karyo-vmware-agent`, even when session
autostart discards stderr. Diagnostics do not restart or abort live transfers.

The separate optional [vmblock system component](vmblock/README.md) releases
blocked file transfers only during system shutdown, before session teardown.
It is independently packaged as `karyo-vmware-shutdown`; it does not give the
ordinary coordinator privileges or change the native vendor tools.
The adapted source checks pass sixteen ordinary cases on Debian and Ubuntu,
including coordinator ownership behavior, diagnostics and shutdown selection.
Both native CMake installations and systemd unit verification pass. Actual
blocked-request shutdown and package lifecycle have separate acceptance records.
