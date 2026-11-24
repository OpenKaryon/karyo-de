# External tunnel integration boundary

The independent DE owns profile namespaces through its captured runtime token,
kernel identities and journal. A provider must integrate with that ownership
model. An installed client or a successful kernel WireGuard fixture does not
establish external-provider compatibility.

## Required ownership contract

The external integration still needs implementation and end-to-end acceptance.
The following describes the required boundary, not an already shipped API:

- A privileged provider obtains the current context from the DE's owner, rather
  than reconstructing it from a username, interface glob or private journal path.
  Its peer identity and passed network descriptor must agree with the captured
  host or active child context. A directory name alone is insufficient.
- Context describes the owned namespace identity, current profile lifetime,
  physical forwarding peers and the existing private resolver object. It must
  not expose application credentials or permit the provider to choose another
  profile's routes or resolver.
- Home tunnel routing and its guard preserve Work's independently owned physical
  carrier path. The provider and DE must agree on marking/routing/firewall
  ownership. Adding an accept rule to an unrelated nft base chain does not prove
  that another provider's later chain permits the packet.
- Work DNS remains attached to the same captured private resolver object. A
  provider must not replace the host resolver or manufacture a legacy path to
  obtain another profile's DNS access.
- Disconnect and profile shutdown release provider-owned routes and DNS before
  the DE retires the namespace. Recovery preserves later changes made by another
  owner. Reconnect reacquires a current context instead of reusing stale token,
  interface or resolver identities.

The DE must preserve its token and captured-identity design. Blind legacy aliases,
renaming interfaces to satisfy the old client, broadening authorization or
silently replacing the external helper would not implement this contract.
The external client remains a separately maintained package input; this review
does not change its repository or import its network-manager implementation.

## Required ordinary acceptance

Use own temporary profiles/endpoints, without user credentials. On Debian and
Ubuntu, test the real released or explicitly rebuilt external application with
Home and Work together. Record both network identities, route selection, DNS
and successful private TCP/UDP payloads; include the selected Work exit-node
case. Keep the Home tunnel connection operational while Work retains its own
NetBird state and resolver. Then exercise normal disconnect, reconnect,
application exit and profile shutdown, checking original data/configuration and
owned cleanup. GUI/tray behavior and a normal authorization prompt need their
own observations. These functional gates remain open; no security test suite is
required or claimed by this integration specification.
