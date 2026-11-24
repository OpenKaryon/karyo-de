# Native main-network preparation checkpoint

The schema-10 offline main-account allocator passes a source-overlay kernel and
resolver check on independent Debian 13 and Ubuntu 26.04.1 QA copies. This is a
network-layer checkpoint for [per-desktop networking](NETWORK_ISOLATION.md), not
an installed-session or VPN-support claim.

## Executed actors

`profiles/tests/native_main_network.py` accepts only the two networking QA
hostnames. It creates three temporary offline accounts, representing Home, Work
and Experiments at the network layer. Each is prepared through the main-account
allocator as a separate standalone context; no family login or child-switch
authority is fabricated. The fixture uses a private configuration/journal root,
the actual network allocator and three actual private resolved/bus services.

Ordinary-user transient processes inherit the namespace and resolver mounts
from the existing client properties. They resolve their context's own name and
exchange actual TCP/UDP payloads with local endpoints on the captured veth.
Each fixture DNS server returns only its own context's name. A neighboring name
does not resolve through that view. No external account, token or VPN endpoint
is used.

## Observed results on both bases

- All three network namespace identities and private resolver processes differ.
- Each ordinary client passes DNS and TCP/UDP checks while the contexts coexist.
- Home's endpoints, resolver and namespace close through their captured owners;
  Work and Experiments continue passing the same ordinary-client checks.
- Preparing Home again obtains a new runtime token and working DNS/TCP/UDP.
  Neighbor namespace identities and captured resolver unit identities remain
  unchanged before and after this restart.
- Teardown clears the journal and removes only the temporary captured accounts
  and resources. Host routes, rules, links, namespace inventory, forwarding
  setting and resolver contents/target match the initial baseline.

The complementary eight selected journal/model cases pass on native Debian and
Ubuntu as well. They cover offline main preparation, ordinary child allocation,
controller recovery, main/child receipt coexistence and a login appearing during
preparation. Their kernel is simulated; the native fixture above supplies the
separate actual namespace/resolver/packet observations.

## Remaining boundaries

These are temporary source overlays, not installed package acceptance. The
graphical login path, scoped main user manager, system-service/socket views,
physical uplink forwarding and IPv6 still need their own observations. No
WireGuard, OpenVPN or NetBird tunnel is started by this fixture. Its Home/Work
labels must not be substituted for simultaneous graphical managed sessions or
the final VPN matrix.

Runtime defaults remain unchanged. No login-manager/PAM configuration, installed
DE module, host resolver or global forwarding value is changed by this check.
