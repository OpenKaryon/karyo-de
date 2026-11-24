# Profile host-service bus

## Compatibility finding: do not select the proxy as the default

The direct host-bus implementation in [HOST_NETWORK_POLICY.md](HOST_NETWORK_POLICY.md)
supersedes the proxy as the intended session topology. A native follow-up showed
that a root process using the profile-mounted proxy still authenticates upstream
as the proxy's ordinary UID. The original caller PID is also not preserved by
this transport. Successful Ping calls below did not establish administrative
compatibility. The proxy is therefore retained only as the existing explicit
experimental/receipt-compatible path; it must not become the automatic default.
Ordinary system administration must preserve both the real UID and PID.

The following records describe the narrower earlier proxy implementation and
its component evidence, not acceptance of normal sudo/polkit behavior.

`HostBusScope` launches the distribution's unchanged `xdg-dbus-proxy` under the
profile UID, in its captured network namespace. The worker has no effective,
permitted, ambient or bounding capabilities and accepts only Unix sockets. Its
unit invocation, executable, PID/starttime, namespace, runtime and socket are
captured and checked. An existing runtime directory is retained rather than
adopted. This is an internal resource primitive; graphical activation does not
allocate it automatically yet.

The proxy exposes an explicit list of ordinary host services for login, policy
authorization, disks, power, the service manager, accounts, host settings and
Bluetooth. Host NetworkManager, networkd and resolved are absent from this
default view. Network clients connect to the separately owned private network
service bus. A proxy does not combine these two buses. Physical Wi-Fi/Ethernet
selection still requires a host-mediated UI operation.

The filtering model and its peer-policy behavior follow the
[upstream proxy manual](https://github.com/flatpak/xdg-dbus-proxy/blob/main/xdg-dbus-proxy.xml).
The worker runs as the profile account because the proxy's credentials are
visible upstream; a root proxy would change the authorization identity.

## Native evidence

`profiles/tests/native_host_bus.py` runs the explicit two-context fixture only
in the independent Debian and Ubuntu networking QA VMs. Each ordinary client
uses the profile proxy through both its environment and the standard mounted
system-bus socket. It receives logind, polkit, disk and systemd replies, reads
policy actions and the disk inventory, and sees the intended service list.
The unchanged nmcli selects the same profile's private connection store. The
same projected client has its private resolver mount and successful DNS,
TCP and UDP payloads.

Both Debian 13 (`xdg-dbus-proxy` 0.1.6) and Ubuntu 26.04 (0.1.7) pass. Separate
UIDs own the Home and Work proxies. Generic recovery stops proxy units before
the network workers, retires network/DNS resources and restores
the host baseline. The current functional selection passes on both bases.

These calls are not a graphical authorization-prompt test, an actual mounted
USB-device test, or proof that the whole DE session already uses this topology.
PAM/main and child entry, ordinary client adapters,
the physical-network UI, portals and installed-session acceptance remain open.
Normal password sudo policy files are unchanged, but operations through this
proxy can lose the elevated system-bus identity as described above.

## Durable ownership

`JournalHostBus` now records the worker invocation, PID/starttime, executable,
namespace, runtime and socket in schema 14, with start/stop intent preceding
native mutation. A fresh controller reconstructs only that acknowledged unit.
An identified unit still stopping is pending, not adopted as a new worker.
An unacknowledged start remains recorded for review.

Offline recovery retires the proxy before VPN/network workers and private DNS.
The account may have only the proxy's completely verified cgroup at that point;
the ordinary no-login/no-process check remains in force for all other account
owners. No process-name or UID-wide bypass is introduced. Graphical consumers
still prevent this offline path from closing their proxy.

On both native bases the two-context fixture reopens the journal in a separate
controller process with both proxies active, reconstructs the same worker
identities and repeats ordinary host-service and private payload calls. It then
kills/recreates Home's proxy while Work retains its proxy and payloads, records
an interrupted stop intent, and completes all retirement through fresh-process
generic recovery. The host baseline and temporary-account cleanup pass.

Automatic main/child allocation is not enabled. The explicit entry options below
integrate their preflight, native user-manager lifetime and PAM handling. This journal gate is not
installed-session or graphical authorization-prompt acceptance.

## Explicit main PAM entry

The root controller can now select `host_bus=True` when preparing a main entry.
This internal option is not exposed as a caller-selected endpoint and does not
change default allocation. Schema 15 records the `host-proxy` main bus view so
old entries reconstruct their original user-manager policy unchanged.

Manager preparation runs while the account is still offline. The controller
then starts and acknowledges the ordinary UID's proxy before handing PAM a
third descriptor: the captured Unix socket opened with `O_PATH`. The PAM module
clones a detached mount from that descriptor. It preserves the real host bus
while `pam_systemd` registers the login, then attaches the proxy socket before
the controller validates the final login attachment. The native user manager
gets the same socket through its separately captured policy. No wire response
contains a socket path to resolve as authority.

Main login and user-manager checks verify the projected socket inode. Namespace
inventory includes the proxy's captured cgroup. Natural logout waits for the
PAM parent and native user manager; only then does it stop the proxy and retire
the manager policy, followed by the existing network-worker/DNS cleanup. It
never stops `user@UID` to force teardown. The privileged PAM parent restores its
original mount and network namespaces during close.

On Debian 13 and Ubuntu 26.04, `native_pam_entry.py --host-bus` passes real PAM
authentication/session entry, ordinary DNS/TCP/UDP and logind/polkit/UDisks
calls, and the same calls from a real `systemd --user` activation. Reopening the
controller before entry and after attach preserves both worker invocations.
Natural exit and a fresh registry remove owned live resources and restore the
host baseline. The two-descriptor resolver/network entry remains supported.
The current functional selection includes its main-entry lifecycle models.

This remains a preallocated main-entry fixture. Automatic production allocation,
installed graphical authorization/portal
behavior and the physical-network UI are still pending.

## Explicit child graphical entry

The root controller can select `JournalSession.start(..., host_bus=True)`.
Schema 16 records the child bus view. Manager preparation still requires an
offline account; the acknowledged proxy starts afterward. The graphical
preflight permits only that proxy's completely verified cgroup alongside the
new login. The compositor and user manager receive the same resolver and
system-bus socket projections, and their live observers verify those identities.

Prepared and closing recovery observe the captured proxy before checking the
account's process inventory. Only its proven cgroup is permitted; other owners
remain subject to the ordinary offline checks. A closing profile waits for its
natural manager lifetime, retires the proxy and then removes the manager policy.
A fresh registry can resume that sequence, including an interrupted proxy stop
intent, before generic network/DNS retirement.

`profiles/tests/native_child_bus.py` runs three explicit modes on the independent
Debian and Ubuntu networking copies:

- `live` starts real labwc and Karyo shell through the installed ordinary entry
  payload, records a desktop screenshot and executes a normal user-manager
  client. The client verifies its network/resolver/socket identities, private
  DNS/TCP/UDP, ordinary logind/polkit/UDisks calls and the unchanged private nmcli.
  A reopened controller preserves the same graphical, manager and worker
  generations and repeats the payload checks.
- `prepared` stops the fixture before graphical unit creation, after actual
  manager/proxy acknowledgement, and recovers their resources in a new process.
- `closing` enters the real graphical session, closes its captured login,
  records an interrupted proxy retirement intent and finishes in a fresh registry
  after the native manager exits.

All modes pass on Debian 13 and Ubuntu 26.04 with the host baseline restored.
Sixty-six selected functional regressions also pass per base. Screenshots show
the real shell on both native bases.

These are preallocated profile contexts with root fixture VT activation, not
ordinary Home/Work family-switch or current-package installation acceptance.
They do not prove a graphical authorization challenge, a physical disk mount,
VPN handshakes in this graphical fixture, or hot replacement of a proxy socket
already mounted in a live session. Standard client adapters, physical networking
mediation, combined installed Home/Work VPN acceptance and IPv6/carrier handling
remain separate requirements.
