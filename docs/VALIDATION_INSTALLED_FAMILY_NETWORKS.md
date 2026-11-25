# Installed family networking checkpoint

The current explicitly enabled runtime prepares a private NetworkManager,
resolver and direct-host UID policy before starting a main login or a stopped
child. Package installation alone remains inactive. Ordinary session startup
recovers only the network endpoint hint from the native user manager, scopes
normal command lookup and supplies the connection-editor menu entry. The real
host system bus preserves caller UID/PID and ordinary authorization semantics.

## Executed actors

Debian 13 and Ubuntu 26.04 independent QEMU guests installed six native profile
and session packages built from DE `c4b30f0` with Builder `269daa7`. LightDM
autologin started temporary Home accounts through the installed main entry.
The ordinary graphical Start command path invoked the installed public frontend
to create Work and switch to it. Neither graphical family request was fabricated
by moving a QA process into a login cgroup.

Both live desktops have distinct network namespaces, resolver objects and
NetworkManager invocations. Ordinary user-manager processes resolve external
DNS, complete certificate-verified HTTPS and exchange physical TCP/UDP payloads
with the other guest. Normal `nmcli` command lookup reaches each private
connection store; the menu's unchanged distribution connection editor uses its
desktop service. Restarting the root profile controller retains both contexts.

## Mixed upstream tunnels

`profiles/tests/installed_mixed_networks.py` uses the existing temporary family,
reviewed ephemeral WireGuard/TLS endpoint helpers and normal installed desktop
clients. The root-only QA state selects the disposable accounts; it grants no
production launch authority. User-manager clients inherit the actual captured
network and resolver. No user's VPN credentials or private provider are used.

Home runs the distribution WireGuard backend and Work the distribution OpenVPN
NetworkManager plugin simultaneously. Both resolve their own private names and
exchange TCP/UDP with their encrypted peer. WireGuard handshake/transfer counters,
the real TLS peer and route-device observations establish tunnel use. Each
private default route selects its own tunnel. The neighboring private DNS name
does not resolve. Controller restart preserves working tunnel payloads, and
disconnect/reconnect of either tunnel leaves the neighbor's identity and
DNS/TCP/UDP working.

Both bases pass. The fixture deletes only the UUIDs returned for its imports,
awaits disappearance of the retired TUN's automatically generated connection,
and retires its captured servers. Host routes, rules, links, namespace listing,
forwarding state, resolver and connection configuration return to baseline;
the host NetworkManager PID stays unchanged. An initial QA cleanup assertion
incorrectly counted the transient `tun0` connection as another import; it was
corrected without changing production behavior and the full gate rerun.

## Remaining acceptance

Schema 19 adds journaled replacement of a captured, fully retired NetworkManager
while the graphical owner stays live. Namespace, resolver, private configuration
and account state are retained. The ordinary host polling loop checks failed
workers one at a time with fifteen-second retry spacing and deduplicated reports.
An acknowledged uplink phase resumes after controller exit; an unacknowledged
process is retained for review. Eighty-four selected profile regressions pass on
both bases, including interrupted replacement/reopen and ordinary retry behavior.

Six packages built from DE `510909a` were upgraded with both real desktops live,
preserving their journal, PAM and administrator configuration. The fixture's
`--worker-failure` gate terminates only the PID/starttime-captured NetworkManager
through a pidfd, first Home and then Work. Ordinary applications observe the
neighbor's encrypted DNS/TCP/UDP throughout automatic host-loop replacement.
The same network, resolver, graphical owner, persistent connection store and
configuration remain. Normal `nmcli` reactivates the saved VPN and encrypted
payloads pass again. The result proves worker recovery plus ordinary reconnect;
it does not claim that a connection with autoconnect disabled silently resumes.
Both bases pass. A further controller restart reads the schema-19 journal and
preserves both recovered worker generations while VPN DNS/TCP/UDP still pass.
Captured fixture retirement and the host baseline also pass.

This is a mixed WireGuard/OpenVPN installed-family result, not completion of the
all-provider matrix. NetBird with independent state, DNS and selected exit node,
normal browser/GUI operation, provider-specific failure/recovery, later desktop entry
while tunnels are connected, physical carrier/DNS changes, IPv6 and final natural
family logout/removal remain separate gates. Current temporary QA families stay
live for those checks; fixture-owned VPN resources have been retired.
