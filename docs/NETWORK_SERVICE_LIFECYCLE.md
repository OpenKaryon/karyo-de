# Private network-service ownership

`JournalNetworkService` owns the unchanged private NetworkManager worker through
the root controller's schema-13 journal. It is an internal resource layer, not
a new VPN protocol. Ordinary clients continue to use NetworkManager, WireGuard
or OpenVPN interfaces. Installed desktop activation does not yet select this
layer automatically; production allocation and graphical bus integration are
separate pending work.

## Resource order

Startup follows acknowledged private network and DNS preparation, before a
graphical login or an optional VPN worker. Its journal entry records the fixed
service kind, transition, persistent state receipt, immutable configuration
receipt and native unit identity. Paths are derived from captured context and
account generation; credentials and commands never enter the journal.

| Transition | Acknowledgement required before advancing |
| --- | --- |
| `state-prepare` | root-owned state directory, owner stamp and account home generation |
| `config-create` | configuration directory/file inodes and packaged keyfile/Netplan backend |
| `service-start` | transient unit invocation, PID/starttime, executable, network and runtime identities |
| `uplink-prepare` | generated temporary base connection active on the captured private interface |
| live | same unit, configuration, mount/socket view and complete worker cgroup |
| `service-stop` | captured unit stopped, PID exited, descendants gone and runtime removed |
| `config-remove` | only the captured configuration file/directory retired |

The directory containing persistent connections stays intact after worker stop.
DNS and network removal are blocked while the worker receipt exists. A stopped
worker can be reconstructed from acknowledged identities for retirement; it is
never presented as a live worker. Creation without an acknowledgement remains
recorded for review. Configuration retirement resumes after an interrupted file
or directory removal without adopting an unrelated object.

The complete captured cgroup is checked, including plugin/service descendants
whose UID differs from NetworkManager. Unit stop uses systemd's control-group
semantics and verifies that no process remains. Stopping just the root PID would
not establish cleanup of an upstream OpenVPN plugin.

## Virtual-device discovery

The worker receives read-only `/sys` while its network-namespace sysctls remain
available. It manages namespace-local software interfaces, not physical host
hardware. The unchanged NM platform selects its non-udev path when sysfs is
read-only. This lets it rediscover an existing WireGuard link after a worker
crash; a host udev initialization event is unavailable in this context. There is
no device-name workaround or client fork. This follows the
[upstream NM platform implementation](https://raw.githubusercontent.com/NetworkManager/NetworkManager/1.52.1/src/libnm-platform/nm-linux-platform.c)
and uses standard
[systemd filesystem/network namespace properties](https://www.freedesktop.org/software/systemd/man/latest/systemd.exec.html).

## Native acceptance and limits

The native NetworkManager, WireGuard and OpenVPN fixtures now use this journal
layer. On Debian 13 and Ubuntu 26.04, a separate observer process reopens the
journal while tunnels remain active and captures the same unit generations and
namespace consumers. OpenVPN has three captured processes per worker: manager,
plugin and dropped-UID client. WireGuard has the manager and a real kernel peer.

Each real three-context protocol gate kills Home's worker through its captured
pidfd, waits for actual systemd/cgroup retirement, resumes an interrupted
configuration removal, recreates the worker and reactivates its saved connection.
Work and Experiments retain their identities, DNS and ordinary TCP/UDP payloads.
Both protocols and both native bases pass. Owned cleanup restores the host
baseline. Selected functional main/DNS/NetBird regressions remain passing.

The [base connection generator](NETWORK_UPLINK.md) now runs after native unit
acknowledgement and before the worker becomes live.

These gates do not prove automatic recovery of an already installed graphical
session. Main/child allocation, namespace service-bus
selection, physical Wi-Fi mediation, mixed providers/NetBird, IPv6/carrier changes
and final installed-session acceptance remain open. Existing resolver-only and
provider receipts retain their previous format and behavior.

## Login observers and generic recovery

Main and child login observers now capture an optional acknowledged network
worker alongside the resolver and optional VPN worker. Namespace inventory
accepts their complete captured cgroups, including dropped-UID OpenVPN children,
while preserving checks on the login and native user-manager cgroups. A dead
worker is reconstructed from its receipt; an exactly identified unit still
draining is deferred until the next controller poll. An already completed login
close is idempotent while that worker finishes. This does not allocate a new
worker in an existing live session.

Offline generic recovery retires the optional VPN worker, network service,
resolver, forwarding and namespace in that order. On both native bases the
three-context WireGuard and OpenVPN gates now release the original controller
observers and use a fresh process running this production recovery path. All
worker receipts and live namespace resources are retired; connection stores
remain private persistent state.

The explicit real-PAM fixture also accepts `--network-service`. On Debian 13 and
Ubuntu 26.04 it prepares the worker before entry, authenticates a disposable
account through the compiled PAM module, starts its native user manager and
checks ordinary DNS/TCP/UDP from that account and a user-manager activation.
Reopening the controller journal before entry and after attach preserves the
same worker invocation. Natural parent/manager exit is followed by normal
registry retirement and an unchanged host baseline. Sixty selected functional
regressions pass on each base, including deferred reconstruction and repeated
session close.

This is a preallocated main-entry fixture, not a graphical login or installed
package gate. Production allocation still uses the older resolver-only path;
the graphical system-bus/socket boundary must be completed before selecting
private NetworkManager automatically. NetBird mixed-provider and IPv6/carrier
acceptance remain separate pending work.

## Live optional provider replacement

Schema 20 adds `restart-start` to the captured NetBird provider lifecycle. The
host first verifies the original PID/starttime, unit invocation, cgroup and
runtime have fully retired. A still-draining captured unit is deferred. The
existing namespace, private resolver, account-generation state and graphical
owner remain in place; only the provider process/socket/endpoint are replaced.

The launch intent clears the old service acknowledgement before starting the
unchanged upstream executable. Startup publishes a fresh endpoint and only
then acknowledges its complete native receipt. A controller interruption with
no started unit can resume; an existing unacknowledged process is retained for
review rather than adopted. Natural login retirement can also finish a recorded
prelaunch intent without starting a new provider. Older complete receipts retain
their schema and interpretation.

Both main and child refresh paths validate their login/manager anchors before
replacing the optional worker. The ordinary bounded monitor detects either a
failed NetworkManager or optional provider, processes one desktop per poll and
keeps fifteen-second retry spacing. Reconnection uses the upstream client's
saved state and realm policy; an intentionally ephemeral realm registration may
require fresh admission after the server deletes it on disconnect.

The selected native functional models pass on Debian and Ubuntu, including
write-ahead launch order, journal reopen, interrupted prelaunch retirement and
provider failure detection alongside a healthy base network worker. The actual
installed-provider failure and reconnection gate remains required separately.
