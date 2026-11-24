# Managed main login

The optional profile packages install inactive entry components. The root host
creates a fresh private network/DNS context before the selected main account's
user manager or graphical command starts. Child desktops keep their existing
independent lifecycle. No VPN provider protocol is required by this PAM handoff.

## Explicit activation

Install the profile accounts, session, host, runtime and login packages first.
Save graphical work and perform this maintenance from a separate root/SSH/TTY
session. LightDM must be inactive and the selected accounts must have no live
login, manager or process. The command does not stop a display manager or user.

```sh
sudo systemctl stop lightdm
sudo karyo-profile-runtime enable
sudo karyo-main-login enable MAIN_ACCOUNT
sudo systemctl start lightdm
```

Replace `MAIN_ACCOUNT` with the actual existing main-account name; additional
main accounts may be listed. This does not create accounts or change passwords.
The administrator's existing runtime enablement controls physical forwarding.

The main command saves an exact root-private activation receipt and original PAM
content under `/var/lib/karyo/main_login`. It inserts only requisite `prepare`
and `attach` session rules around LightDM's existing `common-session` include,
then publishes root-private `/etc/karyo/main_login.conf`. Authentication,
account, password and common-session content remain unchanged. An interrupted
publication may resume from its captured original/target content while LightDM
is stopped; unrelated policy changes require review.

The hook's fixed `desktop=karyo` selector uses the desktop chosen by LightDM.
Upstream sets `XDG_SESSION_DESKTOP` when configuring the selected session and
passes the environment to PAM before opening the session. Other desktops return
PAM_IGNORE without receiving a Karyo network context. This selector is not
privileged execution authority: the host independently captures the root PAM
peer and validates its main-account ownership. See upstream
[session configuration](https://github.com/canonical/lightdm/blob/1.32.0/src/seat.c)
and [PAM session ordering](https://github.com/canonical/lightdm/blob/1.32.0/src/session-child.c).

## Removal

After the managed sessions and their native managers have ended, stop LightDM
and run:

```sh
sudo karyo-main-login disable
sudo systemctl start lightdm
```

Removal uses the captured account selection, withdraws the owned main-login
policy and restores exact original LightDM PAM content. It preserves a later
common-session update and retains the inactive backup receipt. It does not
disable child networking, alter the host resolver, stop a user manager or delete
any account/home. The private-runtime package remains installed and inactive
main-login integration can be reviewed independently of package installation.
After completed removal, a later enable operation may select different main
accounts while retaining the same verified original PAM backup.

## Acceptance scope

The reversible configuration workflow passes with temporary root-owned files on
Debian and Ubuntu, including interruption after the first PAM replacement and
selection of different accounts after completed removal. The actual session-only
module also passes the native PAM fixture with its Karyo selector on both bases.

Separate installed acceptance on Debian 13 and Ubuntu 26.04 builds the five
profile packages through Builder's unchanged native packaging core. Installation
starts no service and leaves LightDM/common-session content unchanged. Explicit
activation then uses an ordinary temporary account/password and the stock GTK
greeter to start the installed Karyo Wayland session through real LightDM/PAM.
The PAM parent, graphical shell and native user manager share the captured
private namespace and resolver mount. An ordinary user-manager activation resolves
public DNS, performs verified HTTPS and exchanges TCP/UDP payloads with a
controlled endpoint on the other VM's physical address.

Restarting the actual installed host service changes its controller PID while
preserving the shell, login, namespace, resolver and manager/DNS service lifetime
identities. The same packet checks pass again. Ordinary compositor logout ends
the main context and its user manager naturally; the host retires its captured
policy, DNS, forwarding and namespace without stopping user@UID forcibly.

With main-login policy still enabled, the same temporary account then enters
stock Xfce through LightDM. Its session and native manager use the host namespace
and resolver, with no private runtime receipt or manager policy. Explicit removal
restores the original LightDM PAM content. QA cleanup removes the temporary
account, disables its test runtime and restores the prior forwarding setting.

These observations establish the installed main-login gate, not the complete
requested VPN architecture. Full system-bus/network-control socket views, real
simultaneous Home/Work VPNs, IPv6 and upstream client/service lifecycle acceptance
remain required. No VPN starts in this main-login gate.
