# Per-desktop networking

Status: architecture with incremental implementation and native acceptance.
Main-account login is implemented and tested as described in
[main entry validation](VALIDATION_MAIN_ENTRY.md). The independent
[NetworkManager primitive](VALIDATION_NETWORK_MANAGER.md) passes its native
service gate. The [installed family checkpoint](VALIDATION_INSTALLED_FAMILY_NETWORKS.md)
now accepts automatic private Home/Work entry, the supported IPv4 pair matrix
of stock WireGuard, OpenVPN and NetBird, selected exit routing, late desktops
and live worker recovery. Hardware Wi-Fi, remaining browser/tray observations
and final family retirement still have separate acceptance boundaries.
This plan supersedes the earlier target in which
the main desktop used the host network and external clients needed a bespoke
provider lease before any integration could work.

## Runtime boundary

Every managed desktop, including Home, Work and subsequently created desktops,
gets its own Linux network namespace and resolver context. The host keeps the
physical Wi-Fi/Ethernet connection, address acquisition and the forwarding
infrastructure. A desktop receives an independent physical uplink through that
infrastructure; its traffic does not traverse another desktop's VPN by default.
The namespaces share the kernel, not their interfaces, routing tables or local
firewall rules. This is not a separate virtual machine for each desktop.

The same boundary applies to the compositor session, applications, user-manager
activation and privileged VPN workers. Moving only the GUI is insufficient:
an application must not command a shared host VPN daemon from a private desktop.
A main-account label or account privilege does not authorize host-network VPN
operation. Ordinary sudo remains an explicit administrative mechanism, not a
promise of containment against a system administrator.

DNS also needs a private filesystem and service view. A network namespace alone
does not isolate /etc/resolv.conf, pathname Unix sockets or a shared system bus.
Each desktop therefore gets its captured resolver, resolver configuration view
and network-control endpoints. Global resolver/NetworkManager access must not
be accidentally reachable through an inherited bus or a default socket path.
Explicit host operations such as selecting physical Wi-Fi remain mediated host
operations; they are not equivalent to unrestricted network-service access.

Family clipboard and other intentional desktop bridges remain separate from
network control. Per-desktop VPN instances have separate configuration, state,
locks and control sockets. Executables and package payloads remain shared:
separate instances do not require duplicate installations or binary forks.

## Existing clients first

Support ordinary, unmodified WireGuard, OpenVPN and NetBird implementations.
The DE owns environment preparation, authenticated launch, instance selection
and cleanup. Package-owned adapters use upstream configuration/control surfaces
instead of requiring the clients to implement a new DE wire protocol.

NetworkManager and its existing plugins are candidates for ordinary connection
management. A separate instance, its bus, activation and DNS integration inside
each context require an actual feasibility test; the shared host daemon must
not be assumed to manage arbitrary private namespaces. Direct upstream tools
remain possible backends where their lifecycle is simpler and well defined.

An adapter must account for all privileged services used by its client, not just
its main process. GUI/CLI activation must select the intended instance, browser
authentication must return to that same context, and service activation must
not silently reach the host. Client-specific launch options belong in the
adapter; no namespace name, interface prefix or subnet inferred from a username
is an ownership contract.

The generic provider API remains an optional extension for applications needing
cooperative control. It is not a prerequisite for ordinary supported upstream
VPNs and must not make them depend on a DE-specific patch. The existing
network-info operation remains inspection-only; it authorizes no mutation.

## Host and desktop ownership

The host owns physical connectivity and forwarding. A desktop worker manages
only its own tunnel and the agreed routes/firewall rules inside its context.
It receives neither arbitrary host-network execution nor writable host resolver
configuration. The DE retains ownership of context creation and retirement.

Private DNS can use existing resolver interfaces in the captured desktop
service view. A cooperative provider can use narrower broker operations.
Whichever path is selected must support the upstream client without exposing a
different desktop's resolver or requiring a new protocol in that client.

Creation, worker registration and retirement extend the captured runtime journal.
Launch revalidates the actual caller and desktop lifetime; user-supplied PID,
UID, label, environment variable or namespace path cannot select authority.
The host launches workers in the owned context. Clients do not receive ptrace
or namespace-entry capabilities merely to discover that context.

On stop or crash, the DE controls the registered worker and descendants and
checks resource cleanup before retiring resolver/forwarding/namespace objects.
An acknowledgement alone is not evidence of cleanup, and a passed descriptor
cannot be revoked by a message. Uncertain objects remain recorded for recovery.
Network changes and later desktop creation must preserve every other live
desktop's independent physical path.

## Implementation order

1. Extend the context model and preparation lifecycle to the main account.
   Prepare its network, resolver and activation environment before launching
   the graphical session. Preserve login-manager authentication, lock/switch
   behavior and user data. Do not attempt to move a running desktop into a new
   network namespace process by process.
2. Reuse the captured network/DNS/forwarding lifetime for every desktop. Test
   ordinary IPv4/IPv6, DNS, loopback and private service views before adding VPNs.
   Verify independent physical routing when desktops start in either order.
3. Add a neutral fixture worker through the same launcher used by real adapters.
   Establish journaled start, stop, descendants, crash recovery and network/DNS
   ownership without inventing client-specific behavior or claiming VPN support.
4. Add upstream WireGuard and OpenVPN adapters and generalize the current
   NetBird integration. Verify configuration import, instance selection and
   normal GUI/CLI/browser behavior. Evaluate the NetworkManager backend in this
   stage, not as an assumed prerequisite.
5. Run the installed simultaneous-desktop matrix on Debian and Ubuntu. Only
   then stabilize any optional provider transport and client-facing API.

Each step is a reviewable local change with its own functional evidence.
Installing the runtime remains inactive. Explicitly enabled main/child entry now
prepares private services; completing the remaining installed matrix is required
before claiming the whole architecture. Distribution/ISO work and unrelated
products are outside this implementation scope.

## Acceptance

Use disposable desktop accounts, temporary VPN endpoints and temporary
credentials on both native bases. Container-only tests do not establish actual
graphical-session, login-manager or installed-service acceptance.

- Home and Work both have distinct captured network and resolver identities;
  a third desktop exercises the same lifecycle, not a special Work-only path.
- Run each of WireGuard, OpenVPN and NetBird on either desktop with unchanged
  upstream executables. Exercise different VPNs concurrently and two instances
  of the same supported client with separate state and control sockets.
- Observe real DNS and TCP/UDP payloads and expected exit routing from ordinary
  applications. Exercise IPv6 when available and retain explicit limits where
  the selected endpoint supports only IPv4.
- Connect/disconnect/reconnect or terminate one VPN while another desktop keeps
  its resolver, routes, working connections and selected exit node.
- Start a new desktop after another desktop's VPN is connected; change the
  physical carrier and verify independent recovery for the live desktops.
- Complete normal browser authentication and GUI/tray operations in the same
  context as the intended daemon. Neither a CLI-only pass nor a kernel tunnel
  fixture substitutes for this evidence.
- Log out and log back in, remove a desktop, and recover a worker failure.
  Verify captured cleanup, unchanged unrelated settings/data and no orphaned
  provider service. Preserve the existing login/password/lock contract.

The installed Home/Work and mixed-tunnel checkpoint does not meet this complete
matrix. No arbitrary-client support is claimed by this specification alone.
