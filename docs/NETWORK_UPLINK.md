# Generated private NetworkManager uplink

Before the network worker becomes ready, the DE creates and activates an
ordinary temporary Ethernet connection for its captured namespace-local eth0.
Address, gateway and DNS/search policy come from NetworkSpec and DNSSeed. The
context token supplies only its connection UUID; it is not a namespace lookup
or an authorization credential.

The profile uses the unchanged nmcli `connection add save no` interface. Its
active device is checked and the distribution's D-Bus persistence flags must
report `UNSAVED`. No password or user VPN profile is written by this step. Worker
recreation regenerates the base connection; saved user VPN connections remain
in their own persistent state. See the standard
[nmcli connection interface](https://networkmanager.dev/docs/api/latest/nmcli.html).

The journal acknowledges the native unit before the `uplink-prepare` operation.
If connection preparation fails, that unit can still be retired through its
captured receipt. The worker becomes live only after the temporary connection
has activated. No uncertain creation is silently adopted or retried.

## Native evidence

Debian 13 and Ubuntu 26.04 pass the plain manager and real three-context
WireGuard/OpenVPN gates using this production generator. The fixture no longer
adds a special OpenVPN Ethernet profile, changes resolver link DNS manually or
selects a base interface during VPN activation. Ordinary OpenVPN activation finds
the generated base connection. The observed generated-profile flags are `1`
(`UNSAVED`) on both distribution backends.

The physical DNS fixtures bind separate owned test-network addresses on the QA
host. These addresses enter ordinary validated DNSSeed records before resolver
startup, instead of weakening the desktop-address exclusion or adding a
production test-only DNS parameter. Captured loopback address labels and the
full host baseline are verified during cleanup.

Fresh-process observation, Home disconnect/reconnect, normal worker recreation,
forced worker exit and interrupted configuration retirement still preserve
neighbor DNS/TCP/UDP. The plain fixture additionally injects an uplink preparation
failure after real unit startup and retires that acknowledged unit normally.

Installed main/child allocation and graphical service-bus selection remain
separate pending integration. This generator currently supplies IPv4; IPv6 and
live physical-carrier/DNS refresh with active VPNs still need end-to-end work.
