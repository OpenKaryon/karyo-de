# Native simultaneous WireGuard contexts

`profiles/tests/native_wireguard.py` extends the private NetworkManager service
gate with real kernel WireGuard peers, using the unchanged distribution daemon,
nmcli and wg. It runs only in the independent networking QA copies. This is
source-native backend acceptance, not an installed graphical desktop release.

The fixture imports ordinary wg-quick configuration files as ordinary desktop
users. Each private NetworkManager activates its own `wgqa` interface, default
route and DNS through its private resolver. Temporary generated peer keys and
local server endpoints require no user VPN credentials. The stock Ubuntu wg
AppArmor policy stays enabled; exclusive temporary server keys use the standard
`/etc/wireguard` directory and are removed by captured identity on cleanup.

| Base | Distribution packages | Home / Work / later Experiments |
| --- | --- | --- |
| Debian 13 | NetworkManager 1.52.1-1; wireguard-tools 1.0.20210914-3 | passed |
| Ubuntu 26.04 | NetworkManager 1.54.3-2ubuntu3.1; wireguard-tools 1.0.20250521-1ubuntu1 | passed |

On both bases, every context completes a real peer handshake, transfers encrypted
payload and resolves only its own fixture DNS view. Ordinary TCP and UDP reach
endpoints on its peer's tunnel address. Kernel route selection for an unrelated
IPv4 destination selects that context's own VPN interface. Starting Experiments
after Home and Work leaves both existing payload paths operational.

Home disconnect restores its physical fixture DNS/TCP/UDP while Work remains
connected. Home reconnect, normal worker stop/recreation and reactivation retain
its saved connection and preserve the other workers and payload paths. Final
cleanup removes the captured live resources and temporary accounts, and restores
the host route/rule/link/namespace/forwarding/resolver baseline. Host
NetworkManager and Netplan configuration hashes remain unchanged. No generated
key or complete connection configuration is copied into this report.

The local WireGuard peers establish a real encrypted data path, not public VPN
internet egress or an exit-node service. The fixture has no IPv6, physical-carrier
change, forced worker failure, production journal adoption, filtered graphical
system-bus or GUI acceptance claim. OpenVPN and NetBird remain separate gates.
