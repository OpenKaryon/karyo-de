# Optional desktop profile integration

These are reviewed account-management slices of the prototype's independent
desktop sessions. The optional profile-accounts-runtime component packages fixed
privileged account helpers and its creation policy. Installation does not create
users, enable a PAM hook, start/switch sessions or change networking. Logind/VT
switching, protected entry, per-profile network/VPN/DNS and family clipboard
services follow as separately reviewed runtime slices. The current combined test suite is described
in the latest provisioning section below; earlier counts record their own slices.

The model retains the two-level contract: a line contains one main account and
its child desktops; a child creating another desktop adds a sibling under the
same main account. No account can appear twice or attach another family.
Unconfigured users form independent one-member families. Duplicate, nested and
unsafe ownership is rejected rather than partially accepted.

The canonical ownership file is /etc/karyo/desktops.conf. A missing canonical
file can read the legacy /etc/openkaryon/desktops.conf; an invalid/unsafe current
file never falls back. The reader opens one no-follow/nonblocking descriptor,
requires a root-owned regular file with no group/world write, and bounds bytes
and UTF-8 decoding. Privileged writers must additionally validate root-owned
parent directories and perform atomic publication; this model does not write.

PASSWORD=none/home/separate, VPN=none/netbird and WORKSTATION=yes/no are parsed
as literal allowlisted data. No configuration is sourced or executed. Unknown
keys/values and duplicate settings fail. None/home share the main account's
password; separate keeps an independent account password. Home/separate require
the runtime to lock every entry, including first startup; the model alone does
not implement authentication or claim that runtime gate.

Ordinary local account names and multilingual display labels are validated
before becoming passwd/config/unit inputs. Labels reject control characters,
line separators and GECOS delimiters. Derived /30 address allocation preserves
the prototype's UID mapping in 10.231.0.0/16, rejects out-of-range IDs/pools, and
gives distinct host/child addresses. A runtime must still detect conflicts with
existing host networks and prove isolation; these calculations do not configure
interfaces, routes, forwarding or firewalls.

Run python3 -m unittest discover -s profiles/tests -v from a Linux checkout.
All 15 cases pass on native Debian 13 and Ubuntu 26.04. Unprivileged runs pass
with two root-fixture cases explicitly skipped; an additional UID-65534 check
reads a real root-owned fixture and verifies the same ownership. No user,
password, system service, PAM file or network configuration is changed by tests.

The reviewed donor is distro/packages/openkaryon-desktops in prototype revision
26bad17cef413d26c36c117f2f1123dca4edded7. Shared policy is rewritten as immutable
standard-library models with bounded trusted file input and explicit error
handling. New code is GPL-3.0-or-later. The rest of the donor helper is not implied
to have passed review or migration by this policy-only slice.

## Privileged configuration storage slice

ConfigStore adds no executable entry point or service yet. Future root helpers
must hold its bounded interprocess lock before writing ownership or protection
settings. It opens every parent component without following symlinks and refuses
parents writable by another user. New metadata directories are explicitly 0755
and files 0644 even under a restrictive caller umask; lock files remain 0600.

Publication uses a unique exclusive 0600 temporary, complete writes, explicit
mode, file fsync, descriptor-relative rename and directory fsync. A failure before
rename preserves the previous bytes and removes the temporary. A directory-sync
failure after rename is reported as a durability failure; it is not a claim that
the earlier contents were restored. Each file is atomic; multi-file account
operations still need their own checked ordering and rollback policy.

Symlink/FIFO/hardlinked destinations, unsafe locks, invalid current configuration
and unprivileged mutation are refused. The lock has a ten-second default deadline
and does not wait indefinitely. Native tests exercise actual descriptors, root
file replacement, restrictive umask, injected pre-publication failures and an
independent process attempting to acquire the same lock.

The combined suite discovers 26 cases on each base: root runs pass with the one
unprivileged-only case skipped; UID-65534 runs pass with twelve root-fixture cases
explicitly skipped. A separate root-owned read fixture also passes as UID 65534.
All filesystem mutations remain inside disposable container fixture directories;
no installed system configuration, account, password, PAM or route is changed.

## Credential synchronization slice

credentials.py validates ordinary local login accounts, unique names/UIDs and
trusted local password records before planning any update. It does not depend on
remote NSS lookups for password ownership. A main account and its shared children
use one hash; a child configured with PASSWORD=separate stays independent. A
shared child password change synchronizes the owner and other shared siblings
without crossing another family. Empty source hashes lock other shared accounts
instead of enabling empty passwords; the source account itself is left to PAM.

All changes are prepared under the root configuration lock and submitted in one
chpasswd --encrypted batch. Native non-PAM batch behavior validates the complete
input before committing; it also avoids recursive password hooks. The command
uses a fixed absolute executable, a minimal environment, stdin-only hashes,
closed inherited descriptors and a 15-second deadline. Failure messages omit
hashes, input and captured command output. PKEXEC_UID contexts cannot invoke
system synchronization, and the password-change entry requires PAM_TYPE=password.

The native behavior follows [chpasswd(8)](https://manpages.debian.org/trixie/passwd/chpasswd.8.en.html).
Tests run the actual Debian and Ubuntu chpasswd with a private fixture prefix,
locked synthetic hashes and generated account files. They verify shared/separate
and sibling behavior, whole-batch rejection, local UID ambiguity, context denial
and unchanged checksums of the container's real /etc/shadow. No PAM profile or
installed account is changed. Real installed PAM password-change acceptance is
still required when the complete runtime is integrated.

The credential checkpoint discovered 40 cases per base: root runs pass with two
unprivileged-role cases skipped; UID-65534 runs pass with 25 root-fixture cases
explicitly skipped. No executable entry point, service, account creation or PAM
activation is supplied by this credential module alone.

## Safe new-account provisioning slice

AccountProvisioner resolves the authenticated caller by an ordinary local UID.
A child adds a sibling to its main account; no owner parameter can attach an
existing user to another family. Explicit logins use lower-case letters, digits
and dashes. Generated names avoid local accounts/groups, reserved mappings,
existing settings and existing or dangling home paths. Display labels remain
multilingual literal data. Pre-existing home directories are never reused.

The native backend inherits only the prototype's allowlisted groups actually
held by the main account. Explicit empty supplementary membership overrides
useradd defaults too; it does not install any passwordless sudo rule.
A new native account starts with a locked password and an expired account,
blocking password and key-based login during preparation. Settings and family
ownership are published under the same configuration lock; password setup and
expiry removal are the last activation steps. Future session-start helpers must
take this same lock before reading account eligibility.

Before activation, a failure restores family membership and removes only
unchanged newly written canonical settings. Rollback requires the exact captured
new account identity, home device/inode/UID, staging expiry and no user processes.
It never forces userdel or deletes an older account/home. Unproven partial
useradd state, changed root metadata, a running UID or an already activated child
is retained with an explicit administrator-review error instead of deleted.
A failed attempt may leave a canonical copy of the same legacy family mapping;
legacy source files themselves are never modified.

The 57-case profile suite passes on both native bases as root (three ordinary-user
checks skipped) and UID 65534 (41 root-fixture checks skipped). Tests cover actual
file rollback ordering, inactive and already-active failures, concurrent metadata
changes, occupied homes/groups/settings, family boundaries and address capacity.

tests/native-provision.py is a separate real account lifecycle acceptance test.
It refuses any host except the two disposable acceptance VM hostnames, requires
root and reads only a disposable fixture password from stdin. It verifies actual
useradd/chpasswd/usermod activation, shared hashes and inherited groups, a child
creating a sibling with native YESCRYPT, pre-publication rollback and deliberate
post-activation retention. Explicit test teardown removes only identities/homes
proven absent before the test and captured afterward. Main-account hashes stay
unchanged. Both Debian and Ubuntu acceptance guests pass; temporary test users
and homes are removed and original LightDM stays active.

This is native source-overlay acceptance, not a claim that a profile runtime DEB,
root command entry point, PAM hook or desktop service is already installed.
Real installed integration, protected-session behavior and network/clipboard
isolation remain required.

## Login manager inventory and caller identity slice

logind.py reads machine properties using one --property option per loginctl
property, independently verified against both native login managers. Lookups
have a five-second total inventory deadline, at most 256 sessions and bounded
256 KiB pipe output. A hanging native child is killed and reaped. An unavailable
or disappearing session is an error, not evidence that its desktop is stopped.
Only local seat0 user-class Wayland/X11 sessions on real VTs qualify. Matching
uses both local account name and UID; ambiguous inactive sessions fail instead
of selecting an arbitrary switch target. Missing child accounts are omitted.

The privileged caller comes from canonical PKEXEC_UID plus the process's own
kernel session membership through libsystemd sd_pid_get_session(0). A matching
ordinary local account and active graphical logind session are required.
XDG_SESSION_ID, USER, SSH logins, greeter/remote sessions and inactive accounts
cannot supply that identity. Processes in a systemd user manager without a login
session fail closed; a future DBus-activated interface needs an explicit reviewed
peer-credential design rather than an environment-variable fallback.

The 73-case profile suite passes on Debian 13 and Ubuntu 26.04 as root and as
UID 65534, with three and 42 role-specific fixture cases skipped respectively.
The native-logind.py read-only probe was also executed by actual pkexec from a
Terminal launched through each real QA desktop. Both login managers report the
correct active local UID/VT, while root SSH with forged GUI variables is denied.
The temporary QA-only polkit permission was removed afterward. These checks use
a captured source overlay; no production profile executable/PAM/service package
or new desktop switch operation is implied to be installed yet.

Native API references: [sd_pid_get_session](https://www.freedesktop.org/software/systemd/man/latest/sd_pid_get_session.html)
and [loginctl](https://www.freedesktop.org/software/systemd/man/latest/loginctl.html).

## Optional packaged account helpers

The profile-accounts-runtime install component contains isolated-mode Python
entry points under /usr/libexec/karyo and package-owned modules at a literal
absolute path. No executable is imported from the current directory, HOME or
PYTHONPATH. Each entry point selects one fixed operation; it cannot accept an
owner, script, environment file or executable argument from the caller.

Run desktop-add through pkexec from an active local graphical login. Its policy
allows that active user to create a child without asking for the main password;
the helper independently validates the real logind caller and derives the family.
An optional separate password is one complete bounded UTF-8 stdin line followed
by EOF, with a ten-second input deadline. It is never an argv or environment
value. Shared/home modes do not read a password. A retained-account failure has
exit code 2, distinct from a fully rolled-back failure (1); success emits only
the new login as JSON. Unexpected native diagnostics and tracebacks are redacted.

The system-only desktop-sync and desktop-password-changed helpers accept no
arguments. The latter requires the actual PAM password environment; neither can
be invoked as a PKEXEC_UID user operation. No production PAM profile or automatic
account synchronization service is installed by this component yet. In
particular, merely installing the package leaves /etc/pam.d/common-password and
the configured display manager untouched. The shell switch menu remains on its
legacy backend until the complete session runtime is available.

The combined profile suite now has 84 cases, including actual password-pipe EOF,
multiline, size and deadline checks. Native installed-package acceptance and PAM
lifecycle evidence are recorded separately as they are completed; source-overlay
checks in earlier sections are not substituted for those gates.

The profiles directory is also an independent LANGUAGES NONE CMake project.
Building the account-only package therefore runs the same Python suite without
compiling Qt applications; full DE builds include the identical component/test.

## Installed account component acceptance

Exact runtime source 37da8de7603ea97eda68d26c6f10d676858b4017 was independently
packaged on Debian 13 and Ubuntu 26.04, installed in both disposable QA VMs and
called through its production polkit action from a real graphical Terminal.
The installed desktop-add created both a shared-password and a separate-password
child; native shadow checks verified the shared hash and YESCRYPT password.
The test then removed only the newly captured fixture users/homes/metadata.
The main account hash stayed unchanged. Direct root SSH without PKEXEC_UID was
refused, dpkg verification/audit stayed clean, and PAM/default display-manager
configuration did not change. This closes account-helper creation acceptance,
not protected session switching, a global PAM hook or network isolation.

The subsequent launcher refinement disables bytecode writes before importing
package modules, leaving no unowned root-generated cache behind on removal.
The 85-case profile suite tests the actual isolated interpreter against malicious
cwd, PYTHONPATH and PYTHONHOME fixtures, and verifies no bytecode is generated.
It passes on both bases under root and UID 65534, with three and 42 role-specific
cases skipped respectively. Full graphical DE regression remains 68/68 on each
base; account-only packaging runs the identical profile test independently.
