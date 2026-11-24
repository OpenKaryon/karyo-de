# Main-login namespace handoff

This is an optional, session-only PAM module. Installation adds a library and
documentation, not PAM configuration, service activation or a provider API.
It provides no authentication/account/password functions and does not replace
the login manager's existing authentication.

The intended session ordering is a requisite `prepare`, the existing
`pam_systemd` session stack, then a requisite `attach`. An early preparation
failure must stop that stack before it can start a user manager. The host
controller must prepare a captured network,
resolver and inactive user-manager policy before replying. The prepare phase
receives namespace/resolver descriptors from the root-owned desktop socket,
creates private resolver mounts and joins that network before the user manager
and graphical command start. It never accepts a namespace path from a caller.
Attach confirms the actual subsequent PAM/login ownership through the host.
The mount namespace uses slave propagation: later host mounts such as the
systemd-created user runtime directory remain visible, while the session's
resolver/socket mounts cannot propagate back to the host.

Close notifies the host, restores the privileged login parent's original
network/mount namespaces and allows the original PAM stack to close normally.
Actual resource retirement remains the host controller's responsibility after
its captured login and user manager end. A close response is not cleanup proof.

The fixed handoff uses root-only internal login operations and a small descriptor
response. It is not a VPN provider protocol. The host implements these operations
and keeps a durable main-login receipt independently of child-session launchers.
Installed login-manager acceptance remains a separate gate.

The optional `karyo-main-login` command belongs to the private runtime component.
It explicitly enables or removes the two requisite hooks around LightDM's
existing common-session include. It requires LightDM to be stopped and selected
accounts to be offline, saves exact original PAM content before writing and
leaves authentication/account/password rules intact. Package installation alone
does not invoke it. The hooks use `desktop=karyo`: a different selected desktop
retains its ordinary login environment without contacting the network owner.
See `docs/MAIN_LOGIN.md` in the source tree for the maintenance sequence.

Native acceptance is explicit through the disposable QA harness. Successful
handoff alone does not establish full service-bus isolation, physical routing,
graphical Home/Work or upstream VPN support.

The native Debian/Ubuntu source fixture passes real temporary-account PAM
authentication and session opening, namespace/resolver entry before user-manager
startup, ordinary-user DNS/TCP/UDP and activation through that same user manager.
Closing restores the privileged parent's original network/mount context;
captured teardown removes the temporary policy, account, services and namespace.
The fixture now uses the production dispatcher, main registry and journaled
lifecycle, with a locally injected native harness and preallocated network/DNS
context. It reopens the journal before entry, after attachment and after natural
exit. CTest only loads the library and checks its session-only ABI; it does not
run the privileged fixture automatically.
