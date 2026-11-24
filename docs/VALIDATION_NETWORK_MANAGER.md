# Private NetworkManager feasibility

This source-native gate establishes a service primitive, not a complete desktop
VPN environment. The worker now has a separate
[durable lifecycle journal](NETWORK_SERVICE_LIFECYCLE.md); ordinary graphical
desktop activation and default service-bus selection remain pending. The plain
manager fixture activates no VPN and makes no graphical-client acceptance claim.

`profiles/tests/native_network_manager.py` runs only as root in the independent
Debian and Ubuntu networking QA copies. It creates disposable accounts and two
captured main network contexts, private resolvers/buses and unchanged distribution
NetworkManager instances. An ordinary account uses the distribution's unchanged
nmcli against its private bus. Each instance saves an inactive WireGuard profile;
Home is stopped/recreated while Work retains its service identity, connection
store and DNS/TCP/UDP operation. Temporary accounts and owned live resources are
removed; persistent test evidence remains under `/var/lib/karyo-qa`.

| Native base | Daemon | Persistent backend | Two ordinary clients | Home restart / Work unchanged | Private DNS, TCP, UDP / cleanup |
| --- | --- | --- | --- | --- | --- |
| Debian 13 | NetworkManager 1.52.1 | keyfile | passed | passed | passed |
| Ubuntu 26.04 | NetworkManager 1.54.3 | Netplan 1.2 | passed | passed | passed |

The host's NetworkManager and Netplan configuration hashes remain unchanged.
No user credentials, remote VPN account, private client package or client fork
are used. These inactive profiles do not prove a WireGuard handshake.

The worker uses the captured network namespace, a private standard NetworkManager
directory layout and a private resolver bus. Its root process and bus name owner
are checked against the captured unit identity. Persistent state retains the
existing account/home generation stamp. The network-service bus configuration is
an explicit schema-12 view; old resolver-only receipts keep their original bytes.

Ubuntu's distribution daemon persists connections through Netplan, unlike the
Debian keyfile backend. The worker therefore mounts private Netplan inputs and
generated outputs and runs the packaged generator in NetworkManager-only mode
before startup. This regenerates its runtime profiles after worker restart.
The upstream daemon, generator and CLI remain unchanged. See the official
[Netplan integration description](https://netplan.readthedocs.io/en/stable/netplan-everywhere/)
and [NetworkManager configuration documentation](https://networkmanager.dev/docs/api/latest/NetworkManager.conf.html).

The separate [WireGuard gate](VALIDATION_PRIVATE_WIREGUARD.md) and
[OpenVPN gate](VALIDATION_PRIVATE_OPENVPN.md) cover three real simultaneous
tunnels for each protocol on both native bases. The worker now observes the
namespace-local eth0 through a [DE-generated temporary base profile](NETWORK_UPLINK.md).
Neither instance manages host hardware.

The protocol fixtures also pass fresh-process journal reconstruction, full
worker-cgroup retirement after SIGKILL and interrupted configuration cleanup.

Remaining gates: production main/child allocation integration, per-desktop graphical
system-bus and control-socket views, real NetBird and mixed-provider tunnels,
simultaneous VPN routes/DNS and neighbor payloads, worker failure, later desktop
creation, IPv6, carrier changes and installed-package acceptance.
