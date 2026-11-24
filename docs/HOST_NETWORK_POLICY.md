# Direct host bus with private network services

Each managed desktop needs its own network-service bus while ordinary host
services must see the actual caller. A per-UID proxy does not meet that second
requirement: a root process behind it authenticates as the proxy UID. Its reply
to a Ping is not an administrative compatibility check.

`HostNetworkPolicy` keeps the real host system-bus socket. A temporary rule for
the captured profile UID excludes the host NetworkManager, resolved and networkd
service-name prefixes. Stock network clients select the separately captured
private service bus explicitly. Host logind, polkit, disks and other ordinary
services still receive the real caller UID and PID. Root operations remain root
operations; this is service selection, not a replacement for sudo or a security
sandbox for administrators.

The rule uses the distribution daemon's standard `policy user` and
`deny send_destination_prefix` configuration and `ReloadConfig`, without a
patched bus or client. Prefix semantics and policy precedence are documented
in the [upstream D-Bus daemon manual](https://dbus.freedesktop.org/doc/dbus-daemon.1.html).
The reference-daemon configuration on both tested native bases loads vendor
rules before user-specific policies. Custom later or mandatory administrator
policies are outside this acceptance evidence.

## Ownership and retirement

The inactive profile-runtime component installs one loader for
`/run/karyo/host_network_policies`. An empty/missing directory selects no UID.
Installation does not create a desktop rule or restart the system bus. The
runtime directory disappears on reboot; there are no persistent numeric-UID
rules left to affect a later account.

Creation requires the root controller's configuration lock, acknowledged private
DNS, the captured account and an offline UID. Schema 17 records the directory,
file identity and publication/reload/removal intent. The controller acknowledges
a staged `.pending` file before publishing its `.conf` name, then requests a
reload before marking the rule ready. A fresh controller uses the captured inode
and exact bytes. Unacknowledged creation is retained for review.

A rule remains while its login and user manager exist. Natural child teardown
removes the graphical receipt first and retires its rule before DNS. Generic
offline recovery can also finish an acknowledged staged publication, pending
reload or interrupted removal. Removal intent precedes unlink; a new controller
can reload the bus and finish after the file is already gone. Other desktops'
files and the shared directory remain untouched. The existing explicit proxy
and the direct-host policy cannot coexist in one profile receipt.

## Native acceptance scope

`native_child_bus.py live --direct-host-bus` runs a real compositor, Karyo shell
and user manager on each independent Debian 13 and Ubuntu 26.04 networking QA
copy. The ordinary user has the captured network/resolver, private DNS and
successful TCP/UDP payloads, normal logind/polkit/UDisks replies and an unchanged
nmcli connected to the private manager. A direct host-manager call is no longer
selected. The actual system bus reports the real UID and PID for both the
ordinary user and a root process in the same network/resolver view. Reopening
the controller retains the same graphical and network-worker generations.

Additional fixture modes cover preparation before graphical unit creation,
normal closing recovery and interruption after rule unlink but before its
reload acknowledgement. Each retires the temporary account and owned rule and
checks the host baseline.
All four modes pass on both bases. Seventy-one selected functional regressions
also pass per base, including five real-file publication/retirement models.

This remains an explicitly preallocated graphical fixture. Production family
switching, automatic main/child private-manager allocation, standard client
launchers, physical Wi-Fi/Ethernet mediation, real graphical authorization,
current installed packages and combined Home/Work VPN acceptance remain open.
The earlier explicit proxy is not enabled by default and its saved receipts
remain readable.
