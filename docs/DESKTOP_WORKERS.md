# Desktop worker protocol, version 1 draft

Status: implementation contract, not an available installed service. An external
application must retain its explicit unsupported-mode response until the broker,
fixture and installed lifecycle checks described below are present. This document
does not establish application compatibility or replace normal VPN protocols.

## Purpose and ownership

Every desktop, including Home, already has an independently owned network and
resolver/service view. Stock VPN services keep their native control protocols.
An optional package-defined privileged worker needs a way to enter the current
desktop under DE ownership without guessing paths from a username. It must not
gain authority over the physical host or another desktop. Multiple registered
workers coexist with the existing native network services; their receipts form
a separate collection in the desktop journal.

The DE owns the physical carrier, desktop lifetime, kernel namespace, private
resolver and launch/retirement. The provider owns only its declared worker,
transport IPC and the network resources it creates inside that captured context.
No credentials, private application protocol or application-specific name belongs
in this public interface.

## Direct system-bus activation

Use the ordinary host system bus and its root-owned service
org.karyo.DesktopWorkers1. Current desktop sessions retain that direct connection
and install an acknowledged per-UID host_network_policy denying host
NetworkManager, resolved and networkd access. This preserves ordinary native
caller credentials while the worker separately uses the desktop's private
resolver/service view. The GUI's default system/session buses stay unchanged.

The repository also retains an older filtered host-service proxy mode. It must
not carry activation: its host connection identifies the proxy process instead
of the GUI. Activation in that mode is unsupported. Do not bypass it by exposing
another host socket, changing the GUI's default bus, or treating its proxy PID
as the application. No extra activation bus is needed for the current direct
policy mode.

Only the fixed root service owns org.karyo.DesktopWorkers1. The GUI calls directly
from the process which owns its transport; a gdbus, pkexec or other child would
bind the wrong caller. The mediator and root profile owner independently resolve
the original unique sender's native bus credentials, capture PID/start/pidfd and
the actual application cgroup, and match the current captured desktop network,
private resolver mounts and user-manager lifetime. A claimed PID, environment
variable or diagnostic network-info record is never that identity.

Applications in independent user-manager app scopes are supported; membership
in a login session-N.scope is not required. The mediator uses native polkit
system-bus-name authorization for that real sender, then revalidates the caller
before forwarding to the owner and again before publication. A client binds the
activation to the root unique mediator owner and current bus ID, watching owner
loss throughout the operation. Inspection records and seals are not authority.

## D-Bus surface

Object /org/karyo/DesktopWorkers1, interface org.karyo.DesktopWorkers1:

| Method | Input | Output | Behavior |
| --- | --- | --- | --- |
| Activate | declaration ID s, request ID s | activation record h | Async authorization and fixed worker launch |
| Cancel | request ID s | none | Cancel only this sender's pending activation |
| Release | instance ID s, worker generation t | none | Retire only this sender's published instance |
| Retiring signal | instance ID s, generation t, remaining budget milliseconds u | none | Unicast to the bound GUI before forced cleanup |

Request IDs are 32 lowercase hexadecimal characters, unique for a sender/bus
lifetime. Declaration IDs are package-installed reverse-DNS identifiers, not
paths or executable arguments. An activation has a bounded 120-second deadline
including authorization and readiness. Cancellation/name-owner loss/GUI exit
withdraws pending publication and retires any prepared or late-ready worker.
Destroying a client-side async watcher alone is not remote cancellation. The
owner retains a cancelled request until its launch and cleanup have finished;
reusing its ID is rejected. Publication and cancellation are serialized.

The Retiring budget is advisory remaining time when sent. The owner keeps an
absolute monotonic deadline; repeated signals or acknowledgements never extend it.

Errors use org.karyo.DesktopWorkers1.Error with stable suffixes Unsupported,
Unavailable, Unauthorized, Cancelled, Busy and Retained. A Busy/Retained result
does not mean cleanup succeeded. No fallback to a host service is permitted.

## Package declaration and prepared bootstrap

Root-installed declarations live below /usr/share/karyo/desktop-workers. Version
1 declares a fixed executable/argv, package revision, required NET_ADMIN and/or
NET_RAW capabilities, readiness deadline and cleanup deadline. It cannot request
SYS_ADMIN, SYS_PTRACE, a shell command, a username-derived context or arbitrary
host mounts. The owner captures the declaration digest and executable identity
before launch. Runtime caller input cannot replace these values.

The root-installed declaration schema is implemented by worker_declaration.py:

    {"version":1,"id":"org.karyo.Fixture","revision":"1.0-1",
     "argv":["/usr/libexec/karyo/worker-fixture","--serve"],
     "capabilities":["CAP_NET_ADMIN","CAP_NET_RAW"],
     "ready_seconds":5,"cleanup_seconds":30,"stop_application":true}

The loader captures the exact UTF-8 declaration digest and native executable
identity, and rechecks both before use. Readiness is bounded to 30 seconds and
cleanup to 180 seconds. The application-stop policy is explicit. This loader
does not start a worker, grant capabilities or register a production service.
Native ordinary capture/reread checks pass on Debian and Ubuntu.

Worker FD3 is a sealed memfd containing one bounded UTF-8 JSON object, at most
65536 bytes, with duplicate keys and trailing input rejected. FD4 is a dedicated
SEQPACKET lifecycle channel from the owner. It is not the provider GUI channel.
The prepared record is immutable before exec; it is never rewritten into a final
record while consumers can read it.

The prepared schema has version=1, role=managed-worker-bootstrap and the fields
below. Identity pairs mean positive kernel device/inode values; process identity
means PID/start ticks, executable identity and cgroup path plus identity pair.
Opaque IDs are 32 lowercase hexadecimal characters, never bearer credentials.

| Field | Meaning |
| --- | --- |
| owner | boot ID, controller epoch, system bus ID and mediator unique owner |
| desktop | opaque lifetime ID and positive generation |
| worker | instance ID, positive generation, random prepared unit name |
| declaration | ID, revision and SHA256 of installed declaration |
| context | network and GUI source-mount identities; private resolver/bus objects and worker mount-plan digest |
| gui | actual UID/GID, process identity and captured carrier app scope |
| directories | root-owned state/runtime identities and provider endpoint location |
| cleanup | declared bounded deadline and owner lifecycle policy |

The root owner runs the worker inside the captured network/private resolver
service view. The worker gets the private service DBUS_SYSTEM_BUS_ADDRESS and
ordinary resolvectl uses that resolver; the GUI's default bus is not redirected.
The worker gets only declared capabilities, /dev/net/tun when required, private
state/runtime and a read-only cgroup view. The GUI and its transport descendants
stay in the independently captured app scope used for carrier bypass. Putting
only the root worker in a bypass group does not cover GUI transport traffic.

State and runtime are separate per desktop lifetime and worker generation. The
namespace inode alone is not a generation. Parents/leaf objects are captured
without following links. Root-owned bootstrap/state metadata is not writable by
the GUI. A reconnect reacquires current ownership instead of adopting stale
paths, units, socket nodes or resolver objects.

## Ready and final activation

The worker binds its private AF_UNIX listener in the provided endpoint directory.
The GUI connects itself, preserving its actual SO_PEERCRED identity. A root-created
GUI socketpair would report the creator's credentials and is not interchangeable.
The provider's existing listening socket/IPC remains its own protocol.

The worker reports ready through FD4 only after its endpoint is listening. The
owner revalidates the caller, declaration, desktop generation and actual worker
PID/start/pidfd, executable, unit invocation/cgroup and endpoint node/context.
Only then does Activate return a separately sealed activation record FD.

The prepared record cannot contain a not-yet-created worker mount namespace
identity. Its mount plan is fixed; the owner verifies actual worker mounts against
the captured resolver/bus objects after exec and records that namespace only in
the final result.

The final record has version=1, role=managed-worker-activation, request ID,
the unchanged prepared owner/desktop/worker/declaration/context/gui bindings,
actual worker process/unit invocation and mount identity, verified private resolver/bus objects, endpoint node identity and the
prepared-record SHA256. It is returned only on the authenticated activation call.
Seals or root file ownership alone do not authorize a record. A GUI binds the
record to the current bus owner/call and its own current process/context, then
checks the actual root socket peer and endpoint before and after connect.
Owner loss, generation change or a rebound endpoint invalidates the result.

## Retirement and recovery

The owner first records retiring and withdraws publication/new activation. A
worker cannot restore a guard or complete a late open while retiring. The owner
notifies only the bound GUI generation. The GUI cancels pending operations,
discards late TUN results, stops its packet engine, closes all device duplicates
and joins readers before acknowledging its own network stop. Ordinary disconnect
does not require logout.

Through FD4 the owner requests retire for the exact instance/generation. The
worker forces complete release regardless of a previous reconnect guard setting,
reverts only its own resolver links/routes/rules/firewall objects and releases
state locks. Replies distinguish clean, busy and failure. An occupied lock,
process exit or cooperative ACK alone is not proof of cleanup.

The DE independently verifies captured registered consumers and kernel resources.
On timeout it may stop only the exact captured GUI/carrier app scope according
to the declared policy; it never kills every process of a UID. Worker children
must retire too. The private resolver/network stay alive until actual cleanup.
If consumers or owned resources remain, keep the journal and captured objects
for recovery, return Retained and do not reuse that generation. Desktop teardown
normally stops graphical consumers before the final worker cleanup.

## Implementation and acceptance order

First implement a neutral installed fixture, direct sender capture and bounded
activation/cancel/late-ready lifecycle. Then add the root broker, package launch,
private resolver service view, journal collection and two-phase retirement.
External adapters remain disabled until this producer is present and accepted.

On Debian and Ubuntu, ordinary installed checks use disposable profiles and
own endpoints. Exercise a Home worker alongside Work's stock VPN and separate
DNS, with a selected Work exit node; launch Work after Home is already connected.
Observe native GUI authorization, direct GUI/worker identities, route selection,
DNS and real private TCP/UDP payloads. Exercise cancel during authorization,
late ready, GUI exit, reconnect, owner restart and desktop shutdown, checking
owned cleanup and preservation of the other desktop's routes/resolver. These
functional gates are separate from parser/unit fixtures and remain open here.

## Direct-bus fixture checkpoint

profiles/tests/native_worker_bus.py runs its own temporary bus and ordinary
unprivileged caller, a root listener worker and sealed descriptor handoff. It
uses WorkerOrigin to pin the actual process with pidfd, namespace and cgroup
identities; it checks caller PID/start/UID at both D-Bus activation and direct SEQPACKET
connect, root worker peer, cancellation before late publication and endpoint
retirement. It closes only its own processes and temporary sockets.

Run with --user naming a disposable ordinary account (UID 1000..65533), as root
with native python3-dbus, python3-gi, dbus-daemon and runuser in an
isolated Debian or Ubuntu QA guest. The fixture has no installed service name,
network mutations, polkit authorization, desktop binding or production launch.
Its fixture-activation record and simplified Release signature are deliberately
not the production wire schema. Passing it establishes transport feasibility,
not managed-mode availability; adapters must remain disabled.


### Installed desktop-origin checkpoint

The optional --desktop-context experiment additionally binds an ordinary
application service to the actual live main desktop and user-manager invocation,
its network namespace, private resolver directory, real GUI bus mount and
independent app cgroup. On isolated Debian and Ubuntu guests, the temporary
fixture reattaches the captured owner while the normal host controller is
stopped, then restores that controller. It creates no provider network resources.
This requires a separately prepared disposable desktop and is not a command to
run against a personal session. The test deliberately restricts this mode to the
named native QA guests.

The fixture's root echo worker still uses the experiment's own host context.
Successful GUI binding is not evidence of worker placement, production polkit,
package declaration launch, journal retirement or real provider compatibility.
Those gates remain open; there is still no production activation service.
