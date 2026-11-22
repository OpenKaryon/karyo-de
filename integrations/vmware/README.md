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
HGFS, display following and other-DE handover remain separate integration gates.
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
