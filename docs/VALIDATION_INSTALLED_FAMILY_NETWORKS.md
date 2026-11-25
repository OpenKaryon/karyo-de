# Installed family networking checkpoint

The current explicitly enabled runtime prepares a private NetworkManager,
resolver and direct-host UID policy before starting a main login or a stopped
child. Package installation alone remains inactive. Ordinary session startup
recovers only the network endpoint hint from the native user manager, scopes
normal command lookup and supplies the connection-editor menu entry. The real
host system bus preserves caller UID/PID and ordinary authorization semantics.

## Executed actors

Debian 13 and Ubuntu 26.04 independent VMware guests installed six native profile
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

## Subsequent display-manager integration

### Display-manager console coordination

The first Ubuntu main re-entry exposed a real collision: after main logout,
LightDM started its replacement X greeter on VT8 already used by Work, whose
compositor then crashed. The upstream [LightDM allocator](https://github.com/ubuntu/lightdm/blob/main/src/vt.c)
tracks its own display list rather than external desktop reservations. Kernel
VT reservation alone cannot coordinate that allocator.

The optional `profile-lightdm-runtime` component provides a separate LightDM
range starting at VT13 while children retain their VT8..12 pool. Its systemd
entry wraps the unchanged distribution daemon and records the effective range
in that same process's environment before exec. New child allocation verifies
the actual active daemon PID, executable, cgroup and lifetime marker. A legacy
running daemon requires an explicit restart before new child startup. Installing
this component changes the next LightDM invocation/configuration but does not
restart it, enable isolated networking or enable PAM automatically. Existing
administrator command overrides need to preserve this integration.

Main entry now reads the same optional `VPN=netbird` administrator setting as a
child. Its payload preflight happens before allocation; the captured provider
starts after private NetworkManager/DNS and before PAM entry. Plain Home needs
no vendor payload. The optional client package installs a vendor-unit drop-in:
while the explicit isolated-runtime policy exists, starting `netbird.service`
is skipped by systemd. This preserves the upstream package and avoids its
post-install script starting a shared daemon. It does not stop or migrate an
already running host daemon or import any existing credentials.

## Stock NetBird in real Work sessions

Both guests installed seven DE packages from `953a7a6`, preserving live Home/Work
contexts and administrator/PAM configuration. They then installed the original
NetBird and NetBird UI 0.82.1 packages, with the distribution WebKit dependency.
Vendor package integrity checks pass. The vendor post-install service start is
skipped by the packaged DE condition on both bases; the shared daemon/socket,
host routes, rules and resolver remain inactive or unchanged as appropriate.

Ordinary compositor logout retires the earlier Work context without replacing
Home. The administrator selects the optional provider and the ordinary graphical
public switcher starts Work again. A temporary self-hosted test realm enrolls
each private worker through the unchanged ordinary CLI; admission files are
deleted and no user credentials or profiles are imported.

The two real Work sessions resolve each other's private managed name and exchange
nonce-bound TCP and UDP payloads over `wt0`. Their ordinary user-manager processes
retain their captured namespace/resolver. Plain Home cannot resolve the Work
private name; external DNS, certificate-verified HTTPS and physical TCP/UDP keep
working in both desktops. The vendor GUI runs as the ordinary Work UID in the
same network/resolver, and native Wayland captures visibly show Connected after
its normal welcome flow on Debian and Ubuntu. This observes an actual window,
not only a live process or an offscreen launch.

That Work-only checkpoint did not yet accept simultaneous Home/Work NetBird instances,
selected exit-node routing, browser authentication, complete tray interaction or
the final provider teardown. Those remain part of the all-desktop matrix.

## Main logout with simultaneous stock NetBird instances

Both guests subsequently installed eight DE packages from `b53de86`, built with
Builder `4b816c8`, including the optional LightDM console integration. The actual
daemon reports its effective VT13 minimum through the exec-preserved marker;
Home enters on VT13 and Work retains VT8. Ninety-three selected profile
regressions pass on each base, including a real configuration-query child that
writes LightDM's annotated diagnostic output to stderr.

Home and Work both run separate unchanged NetBird workers in their captured
network namespaces, resolver objects and persistent account stores. Their
credentials and assigned addresses differ. Ordinary `netbird` command lookup
selects the calling desktop's worker, and the actual main graphical shell has
its provider hint and catalog. Both desktops resolve external and managed peer
names, complete certificate-verified HTTPS, and exchange nonce-bound TCP/UDP
with the other guest over `wt0`. Because both intentionally join the same
temporary realm, resolving that realm's peer names in both is expected.

`profiles/tests/installed_main_survival.py` exercises ordinary compositor logout
of the real Home login, observes complete retirement of its old network, and
waits for a fresh installed main entry. A fallback greeter stays in the separate
LightDM range (observed VT14). Throughout the cycle, one already-open Work TCP
stream carries repeated payloads over its encrypted peer connection; managed
DNS and UDP also continue. Work's entire captured journal record, including its
graphical owner and provider, remains identical. Debian passes with 3 payloads
before logout, 63 at retirement and 78 after re-entry; Ubuntu passes with
3, 61 and 77. These are counters on one stream, not successful reconnects.

The next ordinary-client probe confirms Home reconnects with its saved
temporary realm registration on both bases, while Work remains connected.
No new admission is needed for this replay. The temporary Home admissions are
non-ephemeral: an earlier ephemeral QA admission was correctly removed by the
realm on disconnect and therefore required re-registration. That test-realm
policy must not be mistaken for lost persistent client configuration.

The initial Ubuntu VT collision remains a recorded failed gate; its old Work
lifetime was retired and replaced before this passing replay. These results
prove survival after the console fix, rather than claiming an uninterrupted
Work lifetime across the earlier failure. Selected exit nodes, browser
authentication, complete tray behavior, provider failure/recovery, physical
carrier changes, IPv6 and final family/realm retirement remain open.

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
