# Desktop worker protocol, version 1 implementation checkpoint

Status: the optional producer, entry and lifecycle collection are implemented.
Installed component payloads pass the ordinary native checks listed below on
Debian and Ubuntu. This is an adapter implementation checkpoint, not acceptance
of an external provider. GUI authorization, GUI-held TUN descriptor retirement,
joined Home/Work payload/DNS and native package upgrade/removal remain open.
An external application must not infer compatibility from a package being present.

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
bind the wrong caller. The D-Bus object lives in the root profile owner itself. That owner resolves
the original unique sender's native bus credentials, captures PID/start/pidfd and
the actual application cgroup, and match the current captured desktop network,
private resolver mounts and user-manager lifetime. A claimed PID, environment
variable or diagnostic network-info record is never that identity.

Applications in independent user-manager app scopes are supported; membership
in a login session-N.scope is not required. The owner uses native polkit
system-bus-name authorization for that real sender, then revalidates the caller
after authorization and again before publication. No forwarded PID RPC or second
mediator credential domain is involved. A client binds the
activation to the root unique profile owner and current bus ID, watching owner
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
     "recover_argv":["/usr/libexec/karyo/worker-fixture","--recover"],
     "resources":{"interfaces":["karyo-fixture0"],
       "route_tables":[{"family":"ipv4","table":42000}],
       "rule_tables":[{"family":"ipv4","table":42000}],
       "route_protocols":[{"family":"ipv4","table":254,"protocol":200},
                          {"family":"ipv6","table":254,"protocol":200}],
       "nft_tables":[["inet","karyo_fixture"]]},
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
below. The concrete record factory is profiles/karyo_profiles/worker_wire.py.
All 64-bit values (device/inode pairs, process start ticks and generations) use
canonical positive decimal strings, without leading zeroes. This avoids rounding
in clients whose JSON number representation uses doubles. UID/GID/PID, version,
mode and bounded timeouts remain JSON integers; D-Bus generation remains type t.
Identity pairs mean positive kernel device/inode values; process identity
means PID/start ticks, executable identity and cgroup path plus identity pair.
Opaque IDs are 32 lowercase hexadecimal characters, never bearer credentials.

| Field | Meaning |
| --- | --- |
| owner | boot ID, controller epoch, system bus ID and root profile unique owner |
| desktop | opaque lifetime ID and positive generation |
| worker | instance ID, positive generation, random prepared unit name |
| declaration | ID, revision and SHA256 of installed declaration |
| context | network, user namespace and GUI source-mount identities; captured private resolver/bus objects and service GID |
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
The worker sends exactly one duplicate listening socket descriptor using
SCM_RIGHTS with ready. The owner verifies AF_UNIX/SOCK_SEQPACKET, SO_ACCEPTCONN,
getsockname, the listener creator's actual worker PID/UID/GID, and the root-owned
0660 endpoint node. The owner verifies private resolver/bus/runtime/state mounts
through the actual captured process, rather than accepting self-reported nodes.
After final caller revalidation it sends commit; the worker acknowledges committed.
Before commit, the worker must not accept application operations or change network
resources. Only then does Activate return a separately sealed activation record FD.

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


### Native worker-entry checkpoint

The optional --managed-entry isolated fixture now exercises a fixed DE same-PID
entry and an InstalledDeclaration-bound transient unit. It publishes an immutable
root-only prepared file, verifies the captured network/resolver/bus/runtime
objects, creates sealed bootstrap FD3 and connects owner lifecycle FD4 before
exec. Native UID0 workers use the captured desktop GID to read private resolver
files without granting DAC override. Their /run is a private read-only tmpfs
with only the current resolver/bus, prepared/control and instance runtime binds;
/dev/net/tun is explicitly passed and cgroup/device limits remain scoped.

On Debian and Ubuntu the real neutral worker reads the sealed bootstrap, verifies
its owner peer, observes its private network and distinct mount namespace,
reads the private libc resolver, calls stock resolvectl, and creates a temporary
TUN link. A scoped retire request closes it. The owner independently observes
process/cgroup completion and the original link inventory before removing only
its captured empty instance objects. Existing native-unit behavior passes its
14 regression checks on each base; three ordinary publication checks include
the new read-only metadata mode. No security/fuzz suite is required by this gate.

The fixture creates its own temporary root entry/declaration in named QA guests;
it is not the installed production D-Bus service. Production broker/polkit,
durable multi-worker collection, owned network-resource receipts and recovery,
real GUI TUN/transport retirement and Home/Work provider payload acceptance remain
open. The bootstrap used by this fixture is an implementation subset, not the
final adapter schema. External adapters must still retain unsupported mode.


### Declared cleanup selectors

The root-installed declaration also fixes a recovery executable/argv and named
resource selectors. Interface names exclude the base lo/eth0 devices. Whole
route-table/rule-lookup claims have an explicit IPv4/IPv6 family; partial route
protocol selectors include family and table. Kernel base tables cannot be
claimed wholesale. Nft selectors identify a family/table pair without exposing
rules, sets or private application protocols to the broker.

Check these selectors before a new launch and after retirement. A link disappearing
is not cleanup: independent IPv6 unreachable routes, marked policy routing and
nft tables can remain. Native fixtures on both bases create only their own
reserved documentation-prefix routes, a private policy rule and an empty nft
table. After TUN closure the owner still observes both route families and all
other claimed objects; only the completed cleanup makes the selectors absent.
No foreign route or physical connection is changed by that fixture.

The helper's state/runtime are ephemeral per worker lifetime. Journaled removal
must record intent separately for runtime, state, prepared and control objects.
A replay may accept the exact planned item already gone; a different inode is
retained. This is separate from the user's application configuration and keys.
Recovery launches get a fresh worker instance and can reference only a captured
prior state directory in the same desktop. The serving mode cannot adopt another
instance's state. Production recovery wiring and joined acceptance remain open.


### Concrete lifecycle records checkpoint

The bounded FD4 protocol is implemented by worker_control.py, separate from the
provider's GUI transport. Every record has exactly version=1, instance (32 hex),
generation (decimal string), and one event or operation name. A retired event
also has state=clean, busy or failure. Records are UTF-8 JSON objects, at most
4096 bytes, with duplicate keys/trailing input rejected. Unknown descriptors,
truncated messages and unexpected lifecycle phases are errors.

The ordinary sequence is ready (+one listening FD), commit, committed, retire,
retired, finish. Recovery uses ready without a listening FD. Only the owner's
finish permits a worker to end its completed lifecycle after independent resource
observation. Owner EOF must force scoped retirement and cleanup; it must never
preserve a reconnect guard. An application channel cannot send owner lifecycle
messages. Cancellation before commit prevents application operations; a late
result is not permission to publish.

The immutable prepared object contains owner boot_id/epoch/bus_id/bus_owner and
PID/start; desktop id/generation and captured manager; worker id/generation/mode/
unit/recovery_from; declaration id/revision/sha256; context network/user/gui_mount/
service_gid/resolver/bus; actual gui identity; directories runtime/state/endpoint;
and cleanup ready_seconds/cleanup_seconds/stop_application. Directory objects have
path and node; resolver/bus objects also carry native invocation/PID/start.

The final object copies this binding, changes role to managed-worker-activation,
adds request and prepared_sha256, worker.process (PID/start/UID/GID/executable/
network/mount/cgroup/cgroup_node/invocation) and endpoint (path/node/UID/GID/mode).
It is a new sealed memfd, not a mutation of FD3. Serving workers cannot use another
instance's state; recovery has a new instance and a captured same-desktop prior
state. Service GID permits private resolver access and can differ from GUI GID.

Native Debian and Ubuntu placement experiments passed the full factory-generated
prepared/final record, listening-FD proof and pre-commit resource absence. They
also passed post-commit resources and retirement observation. Worker cgroups are
included in namespace consumer checks only through captured registered observers.
These are primitive placement checks, not installed D-Bus/polkit or durable
collection acceptance. The external managed adapter must remain disabled.


### Durable collection and recovery checkpoint

Journal schema 21 adds a bounded workers collection independent of native VPN
service records. Each entry records intent before creating instance files or
starting its fixed native unit, then acknowledges the captured objects. Unknown
partial launches remain retained. Worker cgroups participate in the normal
desktop consumer inventory; desktop teardown waits for workers before closing
private DNS, forwarding or the network namespace.

After owner channel loss, reattachment retires the captured instance rather than
publishing an old activation. If declared resources survive the original process,
the owner starts the package's fixed recovery command with a new instance and
the captured prior state directory. Cleanup is observed independently before
per-object journaled removal. An updated package that no longer matches the
captured declaration cannot silently replace the recovery command.

Native Debian and Ubuntu experiments passed journaled launch/retirement,
owner-channel loss and fresh recovery of surviving IPv4/IPv6 routes, a policy
rule and an nft table. These are ordinary disposable desktop fixtures, without
application credentials. The installed D-Bus authorization producer and joined
external-provider payload/DNS acceptance remain distinct gates.

The supported policy is one active VPN per desktop, independently in Home, Work
and other desktops. Multiple worker receipts do not imply VPN stacking. A new
interface-owning worker is refused while another such worker or VPN interface
occupies that desktop. Existing native namespace inventory also retains its
single-tunnel rule. No additional tunnel allowance is introduced here.


The prepared_sha256 field is the SHA256 of the exact FD3 bytes, including the
prepared publication's final line feed. The entry seals the original validated
UTF-8 bytes; it does not reserialize the parsed object. A client or worker must
hash those bytes directly, without reproducing another language's JSON format.
The native Debian application/worker fixture has compared the raw FD3 digest
through its actual private socket with the digest in the final activation FD.


## Installed producer checkpoint

The optional profile-runtime component installs the fixed worker entry and
root-reserved system-bus policy/activation mapping. It shares the existing
karyo-profile-host.service; installing the component does not enable networking
or launch a provider. Profile accounts supply the native polkit action with
auth_self_keep for an active caller. Base account-only host operation does not
import the D-Bus/GI runtime when this optional policy is absent.

The owner runs a real GLib main loop for D-Bus watches and the existing host
socket. Periodically peeking at a GLib context beside an unrelated select loop
can leave method calls queued until other bus traffic, delaying Cancel past its
client deadline. Capturing the real sender is immediate; profile binding and
launch are serialized through an owner queue. Cancel remains responsive during
authorization and is rechecked before commit/publication. Name-owner watches
filter the actual GUI senders and the owner name, avoiding unrelated short-lived
systemctl/busctl connections ahead of cancellation. Read-only credential and
polkit calls use a separate root connection to the same captured system-bus ID;
the service connection and published producer owner remain unchanged.

A fresh Activate by the same still-bound GUI can return a new sealed confirmation
of its current committed worker. It rechecks sender/process/cgroup, owner epoch,
declaration, unit, mounts and endpoint; it does not reuse a retired or former-owner
association. Cancel affects a pending request, while Release requests retirement
of the published instance. Release returning means the request was accepted, not
that resource cleanup has already finished. The private lifecycle and independent
owner observations establish cleanup.

Native checks on both bases use disposable desktop accounts and an installed
component payload produced by CMake. Real polkit text authorization, sealed final
FD, exact raw FD3 digest through actual GUI/worker IPC, native socket peers,
current-worker reacquisition, cancellation of another pending request, scoped
Retiring and journal/cgroup cleanup pass. A separate ordinary case cancels after
the native unit is acknowledged but before delayed ready: no activation is
published and its prepared worker is retired. No permissive authorization rule
or user credential was used; the text agent authenticates the own temporary QA
account. This does not claim observation of the graphical authorization prompt.

The checks in profiles/tests/native_worker_client.py and native_worker_fixture.py
are native QA participants, not installed provider packages. Component staging
does not establish a clean Debian package install, upgrade or removal. Actual
external-provider transport payloads, GUI TUN readers and Home/Work coexistence
must still be accepted with the separately maintained adapter.
