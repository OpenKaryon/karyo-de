# Stock clients for an isolated desktop

`karyo-network-nmcli` and `karyo-network-connections` execute the distribution's
unchanged `/usr/bin/nmcli` and `/usr/bin/nm-connection-editor`. They set the
system-bus address only for that client process. The desktop's ordinary host bus
and session bus are not replaced, and no client fork is maintained.

The DE selects `KARYO_NETWORK_ENDPOINT` for a launch. New main and child
user-manager receipts record `network_client: true`; their captured startup
environment contains this hint, and the child graphical entry receives the same
environment. Reopening or removing their policies uses that explicit receipt
flag, so old user-manager drop-ins remain byte-for-byte reproducible. The root controller
publishes its read-only `client.json` in the captured NetworkManager runtime
directory before acknowledging the worker. The hint contains the desktop UID
and group, namespace and resolver identities, private bus directory/socket/PID
and the network worker PID/invocation. It contains no connection secrets.

The ordinary-user launcher checks its own network/resolver, the root-published
hint, private socket and actual bus peer. A standard read-only D-Bus call checks
the current NetworkManager owner before exec. A missing or changed service
returns an unavailable error; the launcher does not fall back to the host.
There is no username-to-namespace lookup or caller-selected privileged program.
This hint routes an ordinary client, not a credential for root mutations or a
VPN-provider lease protocol. The private bus still authenticates the user.

Schema 18 marks new network-worker receipts with `clients: true`. Their runtime
hint is covered by the captured worker directory's lifecycle and deterministic
contents. A fresh observer verifies those contents against its acknowledged
worker and DNS objects. Stopping the whole worker removes its runtime directory
and hint. A newly created worker publishes its own current generation. Older
receipts without this flag retain their original behavior and do not require a
file that their worker never published.

## Acceptance and remaining integration

On Debian 13 and Ubuntu 26.04, the real graphical native fixture launches an
ordinary client in its captured user manager. The stock nmcli creates, lists
and deletes an unsaved temporary connection through the new adapter; the host
connection store does not acquire that connection. The same operation passes
after reopening the controller with unchanged worker identities. The editor
launcher executes the installed editor's `--help`; this is not GUI-window,
credential-prompt or VPN-editor acceptance.

The fixture allocates the context explicitly, but its ordinary user-manager
client inherits the hint from the actual managed environment. Installed acceptance
of automatic main/child private-service allocation and physical Wi-Fi/Ethernet UI
mediation remain open.
The direct host-bus UID policy remains the intended
ordinary session view; a private network client selects only its own service.

## Normal session entry and command/menu selection

`karyo-network-context` reads just the routing hint from the current process or
the native user manager's standard D-Bus `Environment` property. It emits only
the fixed endpoint path, not shell code or unrelated environment values. The
hint is ordinary launch metadata: privileged operations never authorize it as
an owner. Actual client execution still checks the root-published objects and
its own kernel/resolver context. Selecting a hint does not require a running
VPN worker, so a worker failure does not itself terminate the desktop.

The normal session entry uses this helper after user preferences. A managed
session prepends its packaged CLI/editor aliases to PATH and its connection
editor entry to the application catalog. The executable supplied by the
distribution stays unchanged. The session autostart publishes the same hint
and PATH to ordinary bus/user-manager activations. The default system-bus
address stays unchanged. An unmanaged session with no hint keeps its ordinary
commands and catalog.

On both native bases, a real main PAM client with the hint absent from its
inherited environment recovers it from the actual scoped user manager and
validates the published network service. Positive launcher models cover the
normal command/catalog selection and preserved host-bus environment. These are
not yet installed LightDM/main graphical or full family-switch acceptance.
