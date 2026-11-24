# Native simultaneous OpenVPN contexts

`profiles/tests/native_openvpn.py` extends the native private NetworkManager
fixture with real disposable TLS servers. The distribution's unchanged nmcli,
NetworkManager OpenVPN plugin and OpenVPN executable are used. No user account,
existing remote VPN or client fork is involved.

| Base | Distribution packages | Home / Work / later Experiments |
| --- | --- | --- |
| Debian 13 | NetworkManager 1.52.1-1; network-manager-openvpn 1.12.3-1~deb13u1; OpenVPN 2.6.14-1+deb13u3 | passed |
| Ubuntu 26.04 | NetworkManager 1.54.3-2ubuntu3.1; network-manager-openvpn 1.12.5-1; OpenVPN 2.7.0-1ubuntu1.4 | passed |

Every context completes a TLS peer connection, resolves its own tunnel DNS view
and transfers ordinary TCP/UDP payload to the encrypted peer endpoint. IPv4 route
selection uses that context's TUN for an unrelated destination. Experiments starts
after Home and Work and preserves their payload paths. Home disconnect restores
its physical fixture DNS/TCP/UDP; reconnect and normal worker recreation preserve
the neighboring workers, DNS and payloads. Saved connections survive worker
recreation through the native keyfile or Netplan backend.

The plugin needs an active private base connection. The
[DE generator](NETWORK_UPLINK.md) now creates an ordinary temporary Ethernet
profile for the captured namespace-local eth0. The fixture no longer creates a
special profile or selects an interface when activating OpenVPN. Physical
Wi-Fi/Ethernet hardware stays on the host. The fixture does not enable a new
default service in installed graphical sessions.

The standard system-bus socket path inside the worker is a bind of the captured
private socket. This matters because OpenVPN's packaged up-helper does not keep
the parent's D-Bus address environment. The privileged plugin also needs
`CAP_KILL` to terminate its own OpenVPN child after the child drops to the
distribution's service UID. Without it, an ordinary disconnect left the old
process and TUN alive. The fixture waits for actual TUN retirement before
reconnecting; it does not hide that failure by deleting the old device.

Final cleanup returns host route/rule/link/namespace/forwarding/resolver state
and NetworkManager/Netplan configuration hashes to their baseline and removes
temporary accounts and live servers. Root-only generated test certificates,
server logs and private saved profiles stay on the QA copies as evidence; they
are never deployment inputs or copied into this report.

The [journal lifecycle gate](NETWORK_SERVICE_LIFECYCLE.md) also passes a fresh
observer process capturing all three worker processes, forced worker exit,
interrupted configuration retirement and connection reactivation with neighbors
still operational.

This is source-native backend acceptance, not installed graphical acceptance or
public internet egress. Production allocation lifecycle, graphical
control-socket views, mixed-provider operation, NetBird, IPv6 and physical-carrier
changes remain separate gates.
