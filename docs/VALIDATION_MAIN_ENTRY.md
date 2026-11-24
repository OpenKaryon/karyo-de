# Early PAM entry checkpoint

The optional `profile-login-runtime` component supplies a session-only PAM
library. It contains no authentication/account/password handler and installs no
PAM configuration or active service. Its root-only internal handoff is not an
external provider API. Durable main-entry ownership is implemented separately
from the child-session launcher. The profile host now handles the internal PAM
protocol and tracks main entries alongside child sessions. Explicit main-login
policy and the login-manager PAM changes are still not installed or enabled.

## Actual native fixture

On independent Debian 13 and Ubuntu 26.04 QA copies, the explicit
`profiles/tests/native_pam_entry.py` fixture builds the C++17 library and native
harness. It creates a temporary account/password, captured private network/DNS,
and a temporary PAM service. The normal installed PAM stacks stay unchanged.

The temporary service authenticates the temporary account through `pam_unix`,
runs a requisite network prepare phase, registers the session through the real
`pam_systemd`, and runs a requisite attach phase. A root transient harness is
the actual PAM caller. The fixture host checks that captured process, its start
identity, executable, subsequent login and namespace/resolver view. Only that
process receives the two captured descriptors.

Preparation installs private resolver mounts and joins the network before the
systemd user manager starts. An ordinary child then checks its actual namespace
and resolver inode, resolves the private fixture name and sends real TCP/UDP
payloads. It additionally activates another ordinary process through the actual
user manager; that process passes the same namespace/resolver/DNS/packet checks.

Closing restores the privileged PAM parent's original mount/network namespaces.
The controller waits for the native user manager's natural lifetime, removes its
captured temporary policy and retires DNS/network/account resources. Both bases
return to the host route/rule/link/namespace/forwarding/resolver baseline.

## Mount and ordering findings

A received resolver directory FD belongs to the controller's mount namespace.
The module uses `open_tree(OPEN_TREE_CLONE | AT_EMPTY_PATH)` and `move_mount` to
attach a detached mount to the new session view, rather than a pathname bind of
the old namespace's `/proc/self/fd` object.

The new mount namespace uses slave propagation. Host-created mounts such as the
later `/run/user/UID` tmpfs remain visible; session resolver/socket mounts cannot
propagate back to the host. Full private propagation initially hid the new user
runtime mount and prevented user-bus activation despite working DNS/TCP/UDP.
The corrected ordinary activation scenario passes on both bases.

Prepare must be requisite: an entry failure must stop before `pam_systemd`
starts a user manager. The first failed fixture exposed this ordering issue and
retained its objects; its one-off QA cleanup is not claimed as production crash
recovery. Subsequent fixtures persist their prepared/active manager receipts and
complete normal captured teardown. The failed logs remain local evidence.

## Build and remaining gates

Release builds, the nonprivileged session-only ABI loader check and inactive
component staging pass on both bases. Staging contains the PAM library and
documentation, with no `/etc` policy, executable launcher or automatic activation.
The account-only standalone project remains compiler-free by default; the full
DE builds the inactive library unless its option is disabled.

Main-entry lifetime uses the actual `MainPeer`, `MainEntry`, `MainRegistry` and
`HostDispatcher` implementations and journal schema 11. A locally injected
root-owned harness and preallocated network/DNS context replace LightDM and the
production allocation prerequisites in this test. A separate `main_login`
receipt records the original privileged PAM parent,
prepared manager policy and eventual logind/manager owners. It does not invent
a child source session or describe the PAM parent as the compositor.

The fixture releases observers, closes and reopens the durable journal, then
reattaches before the first context-FD reply and after the real PAM attach. It
also restores the closing entry after natural parent and manager exit. These
checks pass on both bases while retaining working ordinary DNS/TCP/UDP and
user-manager activation. Captured namespace consumers are checked against the
real PAM login and user-manager cgroups. A close hint alone never stops the
parent or user manager. Selected functional receipt, registry, inspection and
DNS-refresh cases also pass on both bases; the separate security suite is not
run.

The host recognizes captured root PAM parents independently of its existing
authorized desktop helpers. New entries require root policy at
`/etc/karyo/main_login.conf`, containing version 1 and an explicit `users` array;
the reader requires mode 0600. Missing policy leaves accounts unmanaged. A
captured attach or close can still finish after policy removal. The default
allocator checks private-runtime opt-in, physical forwarding and resolved before
preparing an offline account. Production peer selection accepts LightDM's
privileged process; test executable/cgroup substitution is local construction,
never a wire field.

The main registry restores before generic offline recovery and excludes its
owned or naturally draining entries from that cleanup. Read-only inspection
recognizes a managed Home as a private context, and the physical DNS monitor
includes acknowledged main entries without treating their PAM parent as a
child compositor launcher.

The harness alone is not a production host-service restart or graphical login
test. A separate installed gate now uses actual LightDM, its stock greeter and
the installed Karyo Wayland session on both bases. It verifies physical IPv4
traffic, a native controller restart, natural logout, Xfce coexistence and PAM
configuration restoration. See [managed main login](MAIN_LOGIN.md) for that
distinct acceptance scope. Full system-bus and network-control socket views,
IPv6 and simultaneous upstream VPNs remain required. Neither the harness nor
the separate main-login gate starts a VPN.
