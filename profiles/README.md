# Optional desktop profile integration

These are reviewed account-management slices of the prototype's independent
desktop sessions. The optional profile-accounts-runtime component packages fixed
privileged account/list/existing-switch helpers and their active-user policies.
Installing that component does not create users, enable a PAM hook, start/switch
sessions automatically or change networking. The separate optional password-sync component
registers the PAM integration described in the latest lifecycle section. Logind/VT
switching, protected entry, per-profile network/VPN/DNS and family clipboard
services follow as separately reviewed runtime slices. The current combined test
suite is described in the latest lifecycle section; earlier counts record their
own slices.

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

## Installed private PAM password-stack acceptance

The installed desktop-password-changed and desktop-sync helpers also pass real
native PAM acceptance on both QA guests. tests/native-pam-password.c calls
pam_chauthtok through the fixed temporary karyo-qa-password service and accepts
only the three predefined fresh fixture usernames on the two QA hostnames.
It reads the disposable new password from stdin, bounds conversation prompts,
omits PAM text diagnostics and erases its in-process password buffer.

The temporary service runs native pam_unix.so yescrypt followed by pam_exec.so
seteuid pointing to the actual installed helper. A main-account change updates
its shared child; a shared-child change updates the main account; an independent
child stays separate. The installed system synchronization command passes too.
All accounts in this experiment are newly created fixtures, including its main
account. Original passwd/shadow/group/common-password bytes remain unchanged
after teardown. The temporary PAM service, fixture identities/homes/metadata and
QA probe executables are removed; no production global PAM hook is enabled.

This verifies the administrative PAM password-update path and installed hook
ordering. The extended probe also sets its real UID to the fresh shared child
while retaining the effective root identity, as a setuid passwd client does.
A wrong current password is refused with every fixture hash unchanged; a correct
current password updates only the shared family. This uses actual native PAM
conversation and pam_unix verification, not mocked authentication. It does not
claim to execute the distro passwd binary or enable the global pam-auth-update
lifecycle. That lifecycle must remove/disable an enabled hook before its
executable disappears. The low-level package therefore continues to
ship no auto-enabled common-password change until the complete session runtime
and safe maintainer behavior have their own installed acceptance.

The upstream [pam_exec implementation](https://github.com/linux-pam/linux-pam/blob/master/modules/pam_exec/pam_exec.c)
returns success during PAM_PRELIM_CHECK and executes the password helper only in
the update phase. No expose_authtok option is used; synchronization reads the
updated local hash and never asks PAM to export the plaintext password.

## Optional shared-password PAM package lifecycle

The separate profile-password-sync-runtime component supplies the packaged
karyo-desktops-sync password profile. It depends on the account-helper package;
installing that explicit additional package registers the profile through
pam-auth-update --package. The profile is enabled by default when newly selected,
while existing administrator profile selections and local PAM edits remain under
pam-auth-update control. The maintainer never uses --force or edits common-* by
hand. Only the password stack receives the hook; authentication remains native.

On remove/deconfigure, prerm first calls pam-auth-update --package --remove.
It then reads the bounded regular UTF-8 common-password file and refuses removal
if any active line still refers to the helper. Thus a local manual stack which
the native tool refuses to rewrite cannot leave a stale helper reference after
files disappear. Tool failures, malformed/unreadable/nonregular/oversized stacks
also fail before removal. Commented/disabled references do not block removal.
Upgrade keeps the existing integration until the new package configures; postrm
idempotently cleans remove/purge/aborted-install state. No account data is erased.

The script code is exercised with isolated actual shell/pam-auth-update fixtures
for configure, dependency/tool failure, upgrade, remove/deconfigure, purge,
locally modified active references and unsafe common-password input. All 94
profile tests pass on Debian and Ubuntu as root and UID 65534, with three and
42 role-specific skips respectively. Native installed lifecycle evidence follows
separately; this code-level fixture does not substitute for global PAM acceptance.

The native [pam-auth-update manual](https://manpages.debian.org/trixie/libpam-runtime/pam-auth-update.8.en.html)
requires removing profile references before modules disappear, preserving local
configuration and avoiding --force from maintainer scripts.

## Native global PAM and package lifecycle acceptance

The optional password-sync package from exact runtime source
6b9093391bde39bd4cf1b2a4d1ce2c7cc50dca1a is installed and accepted on both
Debian 13 and Ubuntu 26.04 QA guests. Registration adds exactly one helper line
to common-password; other common PAM stacks and account bytes remain unchanged.
Root-only recovery snapshots of original and configured PAM/tool state are kept
on the disposable guests, not included in source or distributed artifacts.

installed-pam.py --global-stack tests both the native common-password stack and
the actual distro /usr/bin/passwd. Administrative passwd changes propagate from
a fresh fixture owner to its shared child. An ordinary child calls the setuid
passwd binary through runuser: a wrong current password is refused without
changing any family hash, and a correct current password updates the shared
family. A separate child stays independent. Test teardown removes only newly
captured fixture users/homes/metadata and restores original account bytes.

The native package also passes administrator disable followed by reinstall
(the hook stays disabled), explicit re-enable, an actual -1 to -2 QA version
upgrade (one enabled hook), remove, reinstall and purge. A harmless extra module
inside the managed block reproduces a locally modified stack. Native removal
fails before deleting files, leaves the custom stack byte-for-byte unchanged and
retains its helper/profile. After restoring that controlled QA edit and matching
pam-auth-update state, removal and purge restore every original common PAM file
and account database byte. The default display manager remains unchanged.

Both guests finish with the ordinary -1 optional PAM package installed/enabled
for subsequent profile-session QA, the original LightDM active and no fixture
users or temporary password services/binaries remaining. The -2 revision is an
ignored QA upgrade artifact, not a change to the ordinary example recipe.
This closes global hook/password-client/lifecycle acceptance; full desktop
start/switch/protected-entry, network/VPN/DNS and clipboard still follow.

## Shared Qt and system policy contract

The greeter/Settings account inventory now reads root metadata through the same
trust boundary as the system profile store: every absolute path parent is
root-owned without group/world write, no parent or final symlink is followed,
final input is a bounded regular root-owned file, and UTF-8/NUL errors fail.
The 64 KiB byte limit applies during reads as well as model parsing. A missing
canonical file may use legacy data; unsafe, malformed, unreadable or dangling
canonical paths never fall back. QFileInfo existence checks are not authority.

Qt and Python independently consume explicit shared policy-corpus.json vectors.
Family boundaries match Python splitlines (including CR/VT/Unicode separators)
and Unicode whitespace, with ordinary ASCII account names of at most 32 bytes.
The same literal password/VPN/workstation parser is available to Qt consumers.
Invalid protection metadata never suggests password-free entry. Settings reads
that root policy for its password source/protection control rather than trusting
OPENKARYON_DESKTOP_PASSWORD or a user session environment override.

Native Qt checks exercise real root files, dangling/parent symlinks, FIFO,
permissions, malformed/truncated UTF-8, NUL, size limits, missing-versus-invalid
legacy handling and protected settings. Both bases also run the Qt reader as
UID 65534 against an actual public root-owned file and a foreign-owned file
inside trusted root directories. The root file is accepted, the foreign file
is refused under both identities. Python discovery now contains 95 cases, with
three ordinary-role skips as root and 42 root-fixture skips as UID 65534.
Installed Settings and Greeter from 2f263df pass the native consumer gate on
both QA guests. installed-policy-ui.py is a metadata-only, fixed-QA-host fixture:
it records original account/PAM hashes, prepares mandatory protection for the
existing QA user, and removes only its known unchanged configuration. With an
empty user environment override, actual Settings displays protection enabled and
read-only; clicking it cannot downgrade the root policy.

A dangling canonical file plus valid legacy map makes the installed greeter
show a configuration error without selectable users or successful submission.
After removing only that fixture, wrong passwords are rejected and the original
QA password opens a real Karyo session through greetd on both native bases.
Recovery restores the original LightDM; account/password/PAM hashes, dpkg audits
and package verification remain clean. Builder VALIDATION-ACCOUNT-POLICY.md
records the exact DEBs, source and evidence. Initial protected-session startup
is still a separate gate; this proof does not claim desktop switching.

## Existing-session switch core

The internal ExistingSwitcher validates the caller's unchanged local identity,
root family membership, target password/expiry and unique graphical sessions
under the same store lock as provisioning. Expired pending accounts, locked
password records and distinct sessions claiming the same VT are refused.

A protected destination receives loginctl lock-session and must confirm a literal
LockedHint before activation. A protected source is also locked before leaving.
The controller rereads identities, active state and required lock hints directly
before activation, then verifies the destination actually became active. One
shared deadline bounds preparation and confirmation. Missing/ambiguous sessions,
vanishing locks, changed identities and nonfunctional lockers fail without an
activation request; a successful method call alone is not a successful switch.

The low-level desktop-list and desktop-switch entrypoints now expose that core
through isolated fixed-path launchers. Both require the actual active local
logind caller, ignoring forged identity environment. Listing accepts no arbitrary
owner/path and reads only the authenticated family under the store lock. Switch
accepts one validated account name, never a chosen session ID, command or owner.
Polkit permits only active local requests; the helper independently checks kernel
session membership. A stopped target returns exit 3 without implicit startup.

Both native bases pass all 120 Python cases as root and UID 65534. This closes
the source entrypoint boundary, not installed authorization or GUI/runtime
acceptance. Creation of
stopped sessions, owned systemd units, VT allocation, protected runtime QA and
network/clipboard integration remain separate stages. No unattended process
launch is added by this component.


## Public unprivileged command interface

The optional accounts package now installs karyo-desktops in the public binary
path. It supports list [--json], switch NAME and add [NAME] --label LABEL
[--protection none|home|separate]. Separate protection additionally requires
--password-stdin; only stdin carries the password, never argv. Every successful
root operation retains its JSON response. A stopped switch target retains the
helper's distinct exit 3; this interface does not silently start a session.

The frontend replaces itself with the absolute native pkexec and a fixed
package-owned helper path, preserving the actual graphical caller session,
stdin and exact exit result. It cannot select an owner, arbitrary executable,
session, configuration path or system password-synchronization operation.
Root helpers independently enforce family, active-session and account policy.
Both frontend and root launchers use Python isolated mode with a literal module
path and no bytecode writes; cwd/PYTHONPATH/PYTHONHOME do not replace imports.

All 129 profile cases pass on Debian and Ubuntu as root (3 role-specific skips)
and ordinary UID 65534 (42 skips). An independent CMake accounts build stages
the public launcher and runs the same suite without building Qt. Native package
and real GUI-caller acceptance of the public interface remain separate checks.


Native profile-accounts DEBs from bf831b0 now pass installed public-interface
acceptance on both guests. Actual Terminal windows launched from each shell
call karyo-desktops list --json and switch karyoqa through native pkexec;
responses contain only the current family and changed=false on VT8. Forged SSH
contexts remain denied. Accounts, common PAM and the default manager are
unchanged; known QA scripts/JSON are removed. This is current-desktop noop
acceptance, not a claim of switching between two distinct sessions. Exact
artifacts and checks are in Builder VALIDATION-PROFILE-FRONTEND.md.


## Installed distinct-session switching

The profile-accounts package from bf831b0 and shell from bb46dfc pass actual
switches between two different graphical users on Debian and Ubuntu. The
fixed-host tests/installed-two-sessions.py fixture creates one proven new shared
child through AccountProvisioner, captures its UID/home identity and protects
root metadata/account/PAM restoration. LightDM starts both native Karyo sessions
on VT8 and VT9; this deliberately does not stand in for owned session startup
or per-profile networking, which are still separate required integrations.

Actual GUI terminals call the installed public CLI/native pkexec in each user
session. With both policies unprotected, Home and Work switch in both directions
without a locker. The fixture then changes only the child's root-owned protection
to home. Inactive target locking confirms before activation; the actual password
form is visible, a wrong password fails and the shared Home password opens the
same session. Leaving that protected source locks it before showing Home, and
returning again requires fresh authentication. Shell PIDs remain unchanged for
both users throughout, including output replacement on Ubuntu.

The fixture is restricted to the two disposable hostnames and exact QA account
names. Cleanup requires the absent-before child identity/home inode and no child
processes/session, removes only unchanged test metadata, and uses non-forced
native userdel. Original passwd/shadow/group/gshadow and common PAM bytes match
afterward; dpkg checks are clean. QA command scripts/JSON are removed explicitly.
See Builder VALIDATION-TWO-SESSIONS.md for installed artifact references, actual
sessions/PIDs, observed password forms and the distinction from stopped-session
startup, shell menu integration and network/clipboard isolation.


## Console reservation for controlled startup

The upcoming stopped-session controller now has a constrained console primitive.
Sessions.occupied_vts reads every logind session's literal Id/VTNr under one
bounded inventory deadline, including console users, greeters and closing owners.
It deliberately does not reuse the selectable graphical-only inventory for VT
allocation. A missing, malformed or disappearing owner fails the attempt.

consoles.reserve_console requires the existing profile configuration lock and
combines that complete inventory with the kernel in-use mask. The fixed pool is
VT8 through VT12, within Linux VT_GETSTATE's representable mask. Only an unused
native character device is opened with no-follow/no-controlling-terminal and
close-on-exec flags. Its descriptor stays held until the caller's preparation
scope ends. New logind ownership, activation or lost busy state invalidates the
lease. Reuse after release or outside the configuration lock is rejected.

This is a cooperative allocation primitive, not an exclusive kernel ownership
barrier against another privileged administrator. The future unit controller
must revalidate before handoff and prove its own target session/unit afterward.
The primitive never activates, resets, hangs up or disallocates a console and
never terminates a foreign owner. It does not itself start a session or configure
networking. Source regressions now total 143 cases on Debian and Ubuntu: root
passes with 3 role-specific skips, ordinary UID with 42. Native installed
open/hold/release acceptance now passes on both fixed QA hosts, as described below.

The Linux UAPI contract was reviewed against the
[Linux v6.12 VT implementation](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/refs/tags/v6.12/drivers/tty/vt/vt_ioctl.c)
and the [kernel man-pages ioctl_vt reference](https://man7.org/linux/man-pages/man2/ioctl_vt.2.html).
Only interface constants/behavior are used; no kernel implementation is copied.


The installed account packages from afdcbbd pass tests/installed-consoles.py on
both native kernels. While VT8 remains active, a held descriptor reserves free
VT9 and a second allocator selects VT10. Both are close-on-exec, revalidated
against logind/kernel state and released without activation. The original busy
mask and selected graphical session are restored; account/common-PAM bytes and
package verification remain unchanged. A released lease cannot be reused.

The first Ubuntu QA assertion incorrectly required the kernel busy flag to
clear immediately on close. A later read found no owner or open descriptor;
a bounded settle check observes release after about 5 ms on both native kernels.
The original failed test is retained. The production allocator conservatively
skips a still-busy console and needs no change to treat it as free prematurely.
Only the fixture waits for settling; it never resets or disallocates a console.
Builder VALIDATION-CONSOLE-RESERVATION.md contains exact packages, hashes and
observed masks. This completes the allocation primitive, not owned unit/session
startup, networking or safe teardown of complete desktop services.

### Owned native profile network primitive

`karyo_profiles.networks.create_network` is an internal root primitive, not a
new privileged command or a running network service. It requires the existing
configuration lock; its future supervisor must first validate the account and
absence of existing consumers. A fresh UUID names each namespace and tags both
ends of its veth pair. The UID selects a /30 in the bounded 10.231.0.0/16 pool.
Existing namespace names, links or overlapping non-default host routes cause
refusal. The code never adopts or deletes an earlier deployment by name.

The lease holds a CLOEXEC namespace descriptor and verifies its nsfs identity
and native type, both link indices, peer relationship and ownership aliases
before teardown. It refuses unknown devices and active namespace processes;
it never kills those processes. Native mutation timeouts are uncertain outcomes,
so unacknowledged resources are retained and that lease cannot create again.
A failed acknowledged setup removes only a completely captured owned pair.
After ambiguous cleanup, the supervisor must retain the lease and report the
need for recovery. There is intentionally no automatic crash recovery or
sweeping of similarly named objects.

IPv6 is disabled inside this new namespace and on its own host veth. The host's
global sysctls, default routes, policy rules, firewall and resolver stay unchanged.
This stage provides only the private point-to-point link and a default route
inside the namespace. Internet forwarding, physical underlay policy, NAT,
private DNS mounts, user-manager and desktop startup are **not implemented by
this primitive**. Do not start an ordinary child session as a substitute.
The native dependencies are iproute2, util-linux and procps; installing the
low-level accounts package alone does not activate this runtime.

Linux interfaces reviewed: [named namespaces and descriptor lifetime](https://man7.org/linux/man-pages/man8/ip-netns.8.html),
[link aliases and peer creation](https://man7.org/linux/man-pages/man8/ip-link.8.html),
and [NS_GET_NSTYPE](https://man7.org/linux/man-pages/man2/NS_GET_NSTYPE.2const.html).
No upstream implementation was copied.

Installed native acceptance: `tests/installed-networks.py` is restricted to the
two disposable migration VM hostnames and takes no arguments. It holds two
leases, reaches each host endpoint with native ICMP, checks absent private IPv6,
refuses a live owned consumer, refuses changed link ownership and a duplicate
subnet, then closes only its own resources. The final host link/route/rule,
namespace, global forwarding/IPv6 and resolver snapshots must match exactly.
Debian 13 and Ubuntu 26.04 pass with source b56892a. Their existing graphical
sessions remain active on VT8 with unchanged shell PIDs. This is acceptance of
the kernel primitive, not an Internet/VPN or complete profile runtime claim.

The first native runs found two real setup constraints: creation-time veth
aliases were not applied, and a gateway was rejected before the peer was up.
The final code tags captured peers explicitly and raises links before routing.
Unknown aliases were retained; the later acknowledged route failure rolled
back its proven resources. The corresponding failure logs and exact artifact
hashes are recorded by Builder in `docs/VALIDATION-PROFILE-NETWORKS.md`.

### Owned physical IPv4 forwarding primitive

`karyo_profiles.underlay` selects a unique lowest-metric live physical IPv4
main-table gateway, requiring a kernel sysfs device, matching ifindex/address,
carrier, an ordinary connected subnet and no ambiguous/multipath configuration.
A lower-metric tun/WireGuard/veth route does not become the physical underlay.
This supports ordinary physical Ethernet/Wi-Fi with a gateway; bridged/bonded,
point-to-point, multipath and IPv6 underlays need further support and are refused
rather than guessed. Interface/DHCP changes require supervisor reconciliation.

`karyo_profiles.forwarding.ProfileForwarding` consumes a captured network lease
under the same root configuration lock, before any consumers start. It requires
host IPv4 forwarding to be explicitly provided by the optional runtime; it never
toggles a global sysctl itself. An unused per-UID routing table contains private
and physical connected routes, a physical default and an unreachable fallback.
Two early RPDB rules pin traffic from and to the private /32 before Home VPN
rules, covering both packet routing and kernel reverse-path validation. Source
priorities use slots 1–16 and return priorities 17–32, enough for the fixed VT
pool; unknown earlier global policies are refused. Physical subnets overlapping
the reserved private pool are also refused. Numeric iproute2
identities avoid aliases masking an already-used routing table. If no safely
earlier priority exists, setup refuses instead of diverting existing rules.

A fresh exclusive nft inet table carries the UUID ownership comment and captured
native handle/complete contents. It masquerades only this private IPv4 source
through the selected physical interface, drops spoofed sources or wrong egress,
and admits return forwarding only from that interface for established/related
traffic. Its input is bounded to one pipe buffer; native calls use no shell.
The code neither flushes a table nor edits a foreign firewall chain. Existing
administrative firewall drops still apply; Docker/firewall-manager cooperation
needs its own tested integration rather than a blanket firewall bypass.

Setup captures each acknowledged route/tag/rule before publishing the graph.
Teardown first requires stopped consumers and exact route/rule/firewall identity,
including refusal if a new foreign rule references the private table. It deletes
only its own rules, firewall handle and individual recorded routes. Uncertain
mutations are retained, not adopted on a retry. The supervisor must close this
forwarding lease before closing the namespace. Persistent recovery, dynamic
reconciliation, private DNS, user-manager/NetBird scope and complete session
startup are separate remaining work; this library alone enables no service.

Installed forwarding acceptance uses `tests/installed-forwarding.py`, restricted
to the two disposable VM hostnames. It explicitly toggles forwarding only as a
fixture, creates a captured blocking Home policy, verifies Home's route/DNS
failure, and tests two live profile graphs concurrently. Each sends a bounded
DNS request and establishes a real external TCP connection by numeric address.
Removing only its captured physical default must hit the unreachable fallback.
Every cycle restores original interfaces, all routes/rules/nft contents, global
and per-interface IPv4 configuration, IPv6 setting and resolver target/hash.

Exact native packages from 088d333 pass three fresh cycles on Debian 13 and
Ubuntu 26.04, with ordinary reverse-path settings unchanged. All twelve DNS
exchanges in those confirming cycles succeed on the first attempt; the fixture
allows at most three bounded UDP sends and reports the actual count. Earlier
single-datagram timeouts, packet traces and the numeric route-type failure are
retained separately. They are not silently reclassified as successful runs.
See Builder `docs/VALIDATION-PROFILE-FORWARDING.md` for exact hashes and limits.
This does not configure a resolver mount or test an actual Home/Work VPN client.

### Private DNS configuration and discovery

`karyo_profiles.dns` reads DNS and search domains from the selected physical
link's typed resolve1 API, checking the physical snapshot before and after.
It does not copy the global resolver file, a host loopback stub or a VPN link's
nameservers. Initial support is IPv4 with ordinary native resolver ports;
IPv6-only seeds, nonstandard ports, authenticated DNS endpoints and encrypted
transport policies fail rather than being downgraded or replaced with public
fallback servers. Client preparation also requires a root-owned canonical
resolved symlink; regular files/other backend paths are refused before mutation.

The private resolved plan generates a dedicated D-Bus policy, isolated runtime
aliases/varlink directory and hidden host networkd metadata. Its resolver daemon
uses only its private system bus; ordinary desktop clients keep the host system
bus for logind/polkit. This matters on Debian where nss-resolve bypasses the
resolv.conf file and calls the varlink socket. Mounting the entire private
/run/systemd/resolve directory gives both libc backends a private view and
preserves visibility across atomic file replacement in that directory.

Bootstrap uses the same per-link SetDNS/SetDomains/SetDefaultRoute API family as
NetBird. The supervisor must prove the private resolver's unit/name owner before
calling it; a namespace-local ifindex must never be sent to the host resolver.
The bus permits only root/service control and profile-user read/query methods.
Configuration files are created in a fresh root-owned UUID directory, held by
inode, and removed only if all captured files remain unchanged and runtime
services/directories are gone. Foreign files, links, replacements or consumers
cause refusal. No service is enabled by installing these library files.

Only DNS/network bind properties are supplied for user clients: capability
bounding sets and normal password sudo are not restricted. The service-specific
resolved sandbox is separate. Complete owned unit supervision, existing-session
mounts, host resolver symlink retargeting beyond supported resolved layouts,
real NetBird daemon lifecycle and UI/callback testing remain required.

### Installed private resolver acceptance

The opt-in root probe tests/installed-dns.py runs only on the two disposable
QA hostnames. It starts captured transient bus/resolver/client services inside
an owned profile network. Native private-bus name ownership must match the
captured resolver PID before bootstrap. A single ordinary client process uses
real libc lookups throughout; Debian takes nss-resolve/varlink and Ubuntu uses
the DNS backend. It retains host logind access and the QA system's existing
sudo rule, has NoNewPrivs=0, can query its private resolver and cannot change it.

Exact installed packages from 8b09128 pass both native bases. After public DNS,
the fixture atomically replaces the global host stub with an unreachable test
server, then retargets the global symlink to the other canonical resolved alias.
The same client still sees private files and succeeds. A private test DNS server
and NetBird-style root link updates change that client's answer without restart;
atomic replacement of the private resolver aliases is visible. Cleanup restores
the original host file/symlink inodes, accounts/PAM, network snapshots and GUI PID.

This covers the two supported canonical resolved targets, not arbitrary resolver
backends or arbitrary global symlink destinations. Installation enables no
service. The full controller, user-manager scope, physical-change reconciliation,
real NetBird authentication/daemon and supervisor recovery remain open.

### Owned private resolver units

The internal services library starts only fresh UUID-named transient DNS bus
and resolver units from the held root configuration lock and host user/network/
mount namespaces. It pins native PID descriptors and executable device/inode,
start time, credentials, cgroup, namespace and systemd InvocationID. Existing
units are refused; a lost launch acknowledgement retains resources rather than
adopting a later name match. Stopping requires the same invocation and process
identity; no reset-failed, foreign-unit replacement or process-name kill occurs.

ResolverServices consumes prepared, reverified resolver files, waits for the
private bus, proves resolve1's process owner before seeding and refuses shutdown
while other namespace clients remain. Installation still activates no services.
The source regressions pass on both native bases; installed acceptance of this
new controller is separate from the earlier private-resolver fixture acceptance.
Persistent recovery and stopped-session/user-manager supervision remain open.

Installed controller acceptance now also passes both QA bases against packages
from b08ce4d. The full resolver replacement/libc/sudo test consumes ResolverServices
rather than the earlier fixture controller. A duplicate lease and stale real
InvocationID are refused without stopping the daemons. Shutdown with a live
client is refused. A verified root process inside the private client namespace
cannot manage host services. Accounts/PAM, host snapshots and shell PIDs remain
unchanged. See Builder VALIDATION-PROFILE-SERVICES.md for exact inputs and limits.

### Offline user-manager network scope

UserManagerScope prepares one fresh root-owned per-UID runtime drop-in only for
an offline profile: no login of any type, matching process, runtime directory,
linger entry or existing private manager policy may be present. It changes
only the network/resolver view; the host system bus and normal sudo policy stay
intact. Full logind owner inventory includes remote, TTY, closing and manager
sessions rather than treating the absence of a selectable GUI as absence.

After the controlled PAM starter creates its one expected login, capture requires
exactly that login and the matching native systemd-user manager record. The
manager's invocation, pidfd, executable/start/UID, init.scope, network namespace
and actual resolver directory mount are checked. Extra logins or stale identities
are refused. This library does not stop user@UID: logind owns its lifetime.
Release waits for the captured manager and all UID logins/processes to end,
then removes only unchanged owned drop-in data and rechecks after daemon-reload.
A concurrent/restarted manager retains the graph rather than losing its network.

Native source regressions pass 223 cases on Debian and Ubuntu in both privilege
roles. Installed acceptance of this scope remains separate. Controlled graphical
session launch, host dispatcher, persistent recovery and normal logout integration
are still required; installing library files activates no profile service.

The first installed scope check finds a native PAM capability leak: a job
started by user@UID retains CAP_WAKE_ALARM in its inheritable set, although its
permitted/effective/ambient sets are empty and normal password sudo works.
The scope now wraps only the verified native default user-manager ExecStart
with setpriv --inh-caps=-all --ambient-caps=-all. It does not set NoNewPrivileges
or shrink the bounding set. Typed systemd D-Bus command records must match the
supported default exactly; custom commands are refused before file creation
rather than silently replaced. Native source regressions now pass 226 cases;
installed acceptance of the corrected capability boundary remains separate.

Installed scope acceptance now passes Debian 13 and Ubuntu 26.04 with source
db70d67. The fixed-host tests/installed-user-manager.py creates a captured new
QA child and inactive PAM session on reserved VT9. Its real user-manager and
activated application share the private network and resolver directory inode.
The activated job has zero inheritable/permitted/effective/ambient capabilities,
NoNewPrivs=0, host logind access and successful normal password sudo. The probe
generates a temporary child password only in memory/stdin and restores the
captured original hash; it saves no test credential in source or evidence.

After the captured PAM session ends, the manager stops through normal logind
lifetime in about ten seconds; the product sends it no stop command. Known QA
account/home/metadata and runtime drop-in disappear, original account/PAM and
network snapshots match, and main graphical shell PIDs remain unchanged.
The probe deliberately uses sleep as the session process, so this does not
claim a complete owned graphical launcher or protected initial-entry gate.
The original PAM session's capabilities still require the separate GUI entry
wrapper; the accepted manager wrapper covers manager-activated applications.

### Controlled graphical PAM session

The internal GraphicalSession library consumes an offline UserManagerScope and
a held free-console lease. It starts a fresh UUID-named PAM unit, captures the
unit invocation and pidfd, then separately captures the logind leader/login and
session.scope invocation/cgroup. Process start time, UID, namespace and manager
scope remain checked. Closing uses only the captured logind scope and original
service, including the case where its leader has exited but scope clients remain.

The separate optional profile-session payload owns the karyo-desktop PAM stack
and fixed entry programs; installation starts nothing and changes no global PAM
stack. Its auth stack denies login authentication: only the already-authorized
root controller opens account/session management. The entry first drops PAM
inheritable/ambient capabilities, then waits for its own logind session to become
active before executing karyo-session. It never activates its own VT. The root
enter operation checks family/current source and confirms source protection
before activation; normal karyo-session retains the initial compositor lock gate.

Native regressions pass 242 cases on Debian and Ubuntu in root and ordinary
roles; the separate entry/PAM payload stages under /usr and /etc. Clean packages
from f2edff3 pass installed graphical acceptance on both native QA bases.
The public host dispatcher and persistent runtime remain separate: this library
is not yet a new public switch endpoint.

The fixed-host tests/installed-graphical.py starts a captured fresh child on VT9,
with no compositor/shell before explicit activation. Real VNC input checks
unprotected, Home-password and separate-password startup and re-entry. A wrong
password keeps initial shell/Files clients absent; correct authentication starts
them. Home/Work roundtrips preserve both shell PIDs and protected re-entry locks
again. GUI processes have zero inheritable/permitted/effective/ambient sets,
NoNewPrivs=0 and the unchanged baseline capability bounding set.

The probe receives QA credentials only through bounded stdin, never from an
embedded value. Separate-password mode temporarily changes only the known new
child and restores its captured hash. Home mode uses its existing synchronized
QA credential without changing the main account. Visual phases require bounded
acknowledgements so captures cannot race the next VT switch. Cleanup restores
original accounts/common PAM, network snapshots and main graphical session.

The first Ubuntu teardown exposed a listed login disappearing before its owner
properties could be read. Owner inventory now retries a complete fresh read
within one deadline; repeated failure never becomes an empty/offline result.
The one retained QA graph was recovered separately by its fixed native
identities, not by adding generic adoption to the product. See Builder
VALIDATION-INSTALLED-GRAPHICAL-PROFILES.md for exact inputs and diagnostics.
The GUI fixture does not claim full VPN/Internet forwarding, a public stopped-
profile operation, persistent recovery or automatic return after user logout.


The optional profile-host component adds a systemd-activated root host transport.
Its filesystem Unix socket is root:root 0600 under the trusted /run/karyo parent;
installation does not enable or start either unit. Unit files use the systemd
lib/systemd/system search directory rather than the architecture library path.
The current public polkit helpers are unchanged and do not contact it yet.

The host captures SO_PEERCRED and a pidfd before parsing a request. Only a root
Python helper in the host user namespace and an active local graphical logind
scope qualifies. The caller is derived from sd_pid_get_session(peer PID), local
accounts and the exact kernel session cgroup; forwarded environment, UID,
owner, session, namespace and command fields are never accepted. Start time,
executable, all UID credentials, cgroup, active VT/session and local account are
revalidated before the operation. A private helper network/mount context is
allowed; the server itself must remain in PID 1's user/network/mount namespaces.

The bounded versioned protocol currently offers family listing and switching
already-running sessions only. It rejects repeated JSON keys, unknown fields,
invalid UTF-8, oversized/truncated/trailing frames and slow/unclosed requests.
Results have sanitized typed errors. A lost reply never retries a mutation.
Stopped targets still return the existing typed error. This transport is not a
persistent profile-resource graph, automatic logout return or recovery journal.
No restart policy is enabled before those remaining ownership rules exist.

266 profile regressions pass in Debian/Ubuntu root and ordinary roles; the
host-only payload stages independently with fixed /usr/libexec and systemd paths.

Native socket activation retains PID 1 as the listener creator seen by the
client's SO_PEERCRED. Root PID 1 is therefore accepted for this server identity;
PID 0 and non-root owners remain rejected. This does not relax the host's peer
rule: an actual request must still come from a root helper PID greater than 1
with a captured active graphical login scope. The first native QA found this
client-side mismatch before an operation; both fresh units were stopped cleanly.

The host also supplies peer reauthorization inside ExistingSwitcher: after
acquiring the configuration lock and immediately before activation (or a no-op
response). A login that ends or loses the active seat while the request waits
cannot be replaced by another session of the same account. The public helper's
existing direct path keeps its current behavior until runtime wiring is ready.

The fixed-host tests/installed-dispatcher.py additionally accepts installed
transport packages on Debian and Ubuntu. Only its own root QA children enter
the captured active login cgroup. The fixture checks native socket activation,
ordinary-user filesystem denial, rejected SSH-root callers, unknown-field
rejection, ignored forged environment and successful listing/no-op switching
from host and private network plus mount namespaces. The controller stays in
PID 1's namespaces. This is kernel peer acceptance, not a GUI pkexec/PAM test.

The QA owns its fresh socket inode and captured service InvocationID/MainPID,
removes only its own empty namespace and stops only those known units. Original
accounts, common PAM, network snapshots and active main login are unchanged;
the optional host remains disabled/inactive. Builder documents exact package
revisions and hashes in VALIDATION-PROFILE-HOST.md. Cross-desktop activation,
stopped-profile startup and persistent resource recovery remain separate gates.


The host now opens a separate lifetime ownership journal under
/run/karyo/profile-host before serving requests. Its directory is 0700 and both
state/lock files are root-owned 0600. The bounded strict schema pins the boot and
lock inode, profile account/source identity, random network token and captured
namespace/veth/MAC receipt. A second controller, changed inode/content, unsafe
file, unknown schema, leftover object or foreign boot is refused without overwrite.

JournalNetwork is the first concrete journal consumer: only an eligible offline
child of the active source may allocate a namespace. It publishes a create
intent before native mutation, captures the complete receipt afterwards and
publishes a removal intent before teardown. Current controller leases can coexist;
old unreconciled records prevent new allocation. Password hashes are not stored.

Host startup can recover this network-only schema by pinning the recorded
namespace inode and checking veth indices, aliases and MACs, unchanged offline
account and absence of namespace consumers. It closes only proven resources,
including an interrupted removal where the pair or namespace is already gone.
Unacknowledged creation, unknown/changed state or live consumers retain the record
and resources for administrator review. This does not adopt by name or stop
processes. Close of the journal itself releases descriptors, never resources.

Later resolver/service/manager/graphical/forwarding receipts need their own typed
checkpoint and recovery before public stopped-profile startup can use the full
graph. Current public helpers remain unchanged. Network-only recovery does not
claim successful recovery of a complete GUI or VPN profile.

285 profile regressions pass on Debian and Ubuntu as root (three skips) and an
ordinary user (82 root/platform skips). New journal cases exercise real file
descriptors, atomic publication and interprocess locks plus captured network
recovery, changed receipts, consumers and interrupted removal. Installed kernel
crash/recovery acceptance is the next gate; unit fixtures do not claim it.

Installed tests/installed-journal.py now exercises actual SIGKILL recovery on
both native QA bases. A fixed child controller creates a real captured network;
its parent kills only that Popen child. Recovery closes the original namespace,
finishes cleanup after veth deletion and recognizes completed namespace removal.
A fourth case triggers the installed socket-activated host service to reconcile
the receipt before serving requests, proving the startup wiring itself.

The unacknowledged-creation case performs no native mutation and proves the
recovery gate retains exactly the original bytes. Only the known synthetic
no-mutation fixture record is cleared by the QA driver; no generic forget/adopt
endpoint is added. Private permissions, controller exclusion, original accounts/
common PAM, network/DNS/forwarding and the main login all pass restoration checks.
The installed host/private-net/mount transport probe also passes after the update.


Journal schema 2 adds typed private DNS configuration and unit receipts while
retaining readable/recoverable schema-1 network records. DNS paths, filenames,
service names and cgroups are derived from the captured profile token; no recorded
program/path is executed. The physical seed and native resolver/profile IDs are
validated, configuration bytes are reconstructed from the fixed ResolverPlan,
and directory/file inodes must match the captured receipt.

ResolverServices exposes internal before/after hooks around its fixed bus and
resolver start/stop operations. JournalDNS uses them to publish intent first,
then capture InvocationID, PID/start, executable/namespace identity and runtime
directory inodes. Restoration opens new observer descriptors and verifies the
same live units without restarting them. Offline startup recovery closes DNS
in dependency order before its captured network; interrupted stop/file cleanup
can finish only under the matching removal intent. Unknown creation, changed
proofs or extra consumers are retained. Later live GUI reattachment remains open.

The low-level host transport does not install or reconfigure a host DNS backend.
The private DNS integration consumes an already configured systemd-resolved
backend with its supported root resolver symlink, dbus-daemon and service account.
An explicit integration dependency/setup package remains separate from the
transport. A missing/unsupported backend is refused before DNS creation.

298 profile cases pass on Debian and Ubuntu in root (skip3) and ordinary roles
(skip87). New cases cover typed DNS state, actual configuration inode/byte
reattachment, partial removal and native-unit identity replacement. The first
fixture attempted chown inside capability-dropped test containers; it now checks
the real current group and wrong-group rejection without requesting capabilities.
Installed DNS journal reattachment/crash acceptance is the next gate.

The new-allocation gate also rejects unresolved create/remove or DNS mutation
intent from the current controller, not only records from an older controller.
Complete current leases still coexist. This prevents a failed write-ahead step
from being bypassed by starting another profile. The regression fixture now
captures its first network receipt before testing a second concurrent lease.
All 300 profile cases pass in root and ordinary roles on both native bases.

Installed tests/installed-journal-dns.py now accepts the DNS journal on both
native QA bases. It creates fixed private DNS resources in a captured child
controller and checks normal close, SIGKILL plus reattachment to the same bus/
resolver PIDs and configuration inodes, completed resolver stop, partial/full
configuration removal and actual socket-activated host startup recovery.
The private D-Bus resolve1 name still belongs to the captured resolver PID;
reattachment performs no service restart. Original accounts/common PAM,
network/DNS/forwarding state and the main login pass restoration checks.

The old network-only crash probe and host/private-net/mount transport also pass
against the final schema-2-capable runtime. This proves DNS-layer observers and
offline cleanup, not complete live GUI reattachment or Internet/VPN forwarding.
See Builder VALIDATION-PROFILE-DNS-JOURNAL.md for exact revisions and artifacts.


Schema 3 adds a prepared user-manager policy and coupled live manager/PAM/
graphical receipts. Fixed drop-in bytes and original policy paths, inode pairs,
native unit invocations, process/executable identities, both logind owner records,
graphical scope invocation, VT and trusted entry/compositor identities are checked.
PAM startup captures its manager and graphical session together before publication.
The entry boundary can reauthorize the same privileged caller before activation.

JournalDNS now separates read-only observer restoration from its strict DNS-only
consumer cleanup gate. JournalSession restores those observers, the exact manager
and compositor scopes, private resolver mounts and namespace consumers. Consumers
must belong to the captured graphical login, user-manager or original transient
PAM service cgroup; normal sudo descendants remain in those owned scopes. Anchor
identity or process churn never turns into evidence of absence.

The host's SessionRegistry holds restored living session observers across requests
and excludes only those verified tokens from offline legacy cleanup. Detaching
observers does not stop the GUI or its services. Network/DNS cleanup refuses a
record that still owns a session. Prepared, incomplete or interrupted closing
transitions still require their later reconciliation path; no public stopped-
profile operation, logout watcher, full forwarding or VPN integration is enabled.

309 profile cases pass on Debian and Ubuntu as root (skip3) and ordinary UID65534
(skip89). The installed live GUI/controller-crash reattachment gate follows next.


Installed tests/installed-journal-session.py now accepts schema-3 living GUI
reattachment on both disposable Debian/Ubuntu hosts. A fixed child controller
creates the owned private DNS, user-manager and PAM compositor graph, then the
parent SIGKILLs only that captured controller. New observers attach to the same
manager/compositor, and the actual socket-activated host retains their pidfds
while answering a request from the captured Work context.

The unprotected case starts the installed editor through the scoped user
manager. Its external VM-console driver acknowledges editor-ready/editor-after
JSON phases on stdin after typing synthetic text and taking screenshots. The
probe checks the same editor PID, an unchanged source file and matching unsaved
recovery-cache bytes before and after controller/host restarts. The Home-password
case remains compositor-locked with the same lock PID and no shell/editor; no
password is supplied or printed. Known orderly teardown waits for native manager
exit, then closes DNS/network and restores the captured QA account/PAM state.
Old offline DNS/network and transport probes still pass on the schema-3 runtime.
See Builder VALIDATION-PROFILE-SESSION-JOURNAL.md for exact artifacts, evidence and
remaining prepared/closing reconciliation and public-runtime gates.


SessionRegistry can additionally reconcile a fully captured offline manager
preparation before any GUI receipt exists, including an unstarted login-start
intent. It requires the same account, no UID/login owners, DNS-only namespace
consumers, the exact prepared manager policy and an absent graphical unit.
Manager-close intent precedes policy removal; DNS/network cleanup runs afterwards
through the existing offline recovery path. Unacknowledged preparation and
interrupted policy removal still retain resources for their later gate.


Installed tests/installed-journal-prepared.py now accepts three actual SIGKILL
cases on both QA bases: captured preparation, unstarted login-start intent and
actual socket-activated host recovery. The final probe runs as a fixed root-owned
file against native packages. No GUI or user manager is created during recovery;
known policy, private DNS and network disappear in dependency order. The previous
living-editor/lock, DNS, network and transport gates also pass on this runtime.
Both original main logins and account/common-PAM/network snapshots are restored;
dpkg verification and final resource postchecks are clean. See Builder
VALIDATION-PROFILE-PREPARED-JOURNAL.md for exact inputs and remaining close gates.


Recorded login-close/manager-close recovery additionally requires all original
process starts and logind IDs to be gone, the exact completed graphical service
and scope (or their absence), an offline account and DNS-only namespace consumers.
It can finish a captured full policy, the same empty directory after unlink or
an already absent directory under manager-close intent. Native policy must still
match the recorded scoped or original baseline form. Changed inodes, files,
invocations, live processes/logins or unknown units retain resources. No process
is signaled and no user manager is stopped by this offline reconciliation.
Installed closing crash acceptance follows separately.


Installed tests/installed-journal-closing.py now accepts all six recorded offline
closing crash boundaries on both QA guests: completed GUI before its journal ACK,
full manager policy, policy file unlinked, policy directory gone, daemon-reloaded
removal and actual host startup from a partial removal. The first case enters a
real compositor before known teardown; other cases own the native waiting PAM
entry and its manager. Each controller waits for natural native manager exit,
then receives SIGKILL only from its captured parent. No fake pidfd or user@stop
is used to infer completion. Old live-editor/lock, prepared, DNS, network and
transport cases pass again on this runtime, with clean final resource/package
checks. See Builder VALIDATION-PROFILE-CLOSING-JOURNAL.md for exact evidence and
the still-required active close/logout supervision and full public runtime.


The long-lived host now polls captured living session pidfds even without client
requests. When an observed compositor exits, ReturnAfterExit checks the same
private journal account/process receipt, family owner, active seat session/owner
and kernel VT. It selects the actual Home session or an existing local service
greeter, confirms a protected Home compositor lock before activation, and leaves
another active seat/VT alone. No fixed greeter console is assumed.

Known session teardown then retries GraphicalPending/ManagerPending while keeping
DNS/network leases; lower resources close only after native logout completes.
The listener uses bounded select polling rather than blocking forever in accept.
Startup replay of already-dead/half-closing sessions and complete public profile
allocation remain separate runtime work. Installed logout/crash acceptance follows.


Installed tests/installed-supervision.py now accepts observed logout, compositor
crash, background exit and initial-lock crash on both Debian/Ubuntu QA guests.
The actual installed host restores the captured living profile, then receives no
further client traffic while its pidfd poll returns Home and finishes teardown.
Exactly one return decision is logged per profile; background exit leaves the
already active Home alone. The ordinary logout command runs through the scoped
user manager; crash signals use only a verified captured QA compositor pidfd.

Original main logins, shells, account/common-PAM and network snapshots are restored.
Old live-editor/lock, offline-closing/preparation, DNS/network and private transport
probes pass again. All 344 source cases pass root/ordinary on both bases, and final
package/resource checks are clean. See Builder VALIDATION-PROFILE-SUPERVISION.md;
source-tested greeter/protected-Home boundaries and still-pending startup replay/
public allocation are distinguished from the installed Home-return scenarios.


Schema 4 adds an optional forwarding layer before DNS. Fixed physical topology,
priorities, ordered captured route/rule rows and comparison-only nft snapshots
with the original UUID comment/table handle are validated. Route deletion argv
is reconstructed from the fixed spec/physical plan, never taken from the journal.
A close intent precedes native deletion; every acknowledged rule, table and route
removal checkpoints its receipt. Restart may reconcile exactly one next fixed
teardown step before resuming, while changed or replaced objects are retained.

Living session observers verify their forwarding lease when present. Cleanup is
GUI/manager, DNS, forwarding, namespace; old schema1-3 records remain accepted.
All 357 profile cases pass on Debian/Ubuntu as root and ordinary users. This
layer changes no global sysctl: explicit forwarding setup and public allocation
remain separate integration work. Installed forwarding journal acceptance follows.


Installed tests/installed-journal-forwarding.py now accepts schema4 on both QA
bases: normal close, living reattachment, all seven real SIGKILL-before-journal-
ACK delete boundaries and actual host startup recovery. A synthetic blocking Home
route/DNS policy remains active while the restored namespace exchanges validated
DNS replies and real external TCP through its captured physical forwarding path.
Only the QA fixture temporarily enables forwarding and restores original sysctls.

Installed tests/installed-supervision-forwarding.py additionally combines NAT,
private mounted NSS/DNS and a living PAM compositor under that same synthetic
Home block. All four logout/crash/background/initial-lock scenarios pass through
the actual host, restore Home and close GUI/manager, DNS, NAT and namespace in
order. Original source state and package verification remain clean. The synthetic
policy is not an authenticated personal VPN. A first three-second UDP probe lost
cold replies; a diagnostic retry worked without route changes. The final fixture
allows at most six one-second attempts with fresh sockets and still requires a
correct response; the underlying transient cause is not claimed as fixed.
See Builder VALIDATION-PROFILE-FORWARDING-JOURNAL.md for exact artifacts, failed
attempts and retained explicit setup/public-allocation/reconciliation gates.


### Explicit private desktop runtime activation

`profile-runtime` is a separate optional installation component. Installing it
adds an inactive networking unit, a host-unit ordering drop-in and the root-only
`karyo-profile-runtime` command. It does not enable services, create users, alter
PAM, enable forwarding or replace `/etc/resolv.conf` during package installation.
The base accounts/session/transport packages remain independently usable.

An administrator must first install the optional runtime dependencies and
configure a working systemd-resolved backend with the standard root-owned
`/etc/resolv.conf` symlink. Then explicitly run:

```sh
sudo karyo-profile-runtime enable
```

The command checks native executables, the controlled PAM stack, installed units,
physical IPv4 underlay and physical-link DNS before publishing a private root
policy. It enables IPv4 forwarding and the root-only host socket persistently.
Home retains the host network; private child desktops use owned NAT and routing
rules. This is currently an IPv4 profile runtime, not generic IPv6 VPN support.
A setup failure is reported as incomplete, with any published policy retained
for administrator review. Retrying `enable` is supported. There is deliberately
no automatic global forwarding reset on service stop, because other software
may also depend on that global setting. Removing the optional package likewise
requires reviewing any enabled profiles and administrator networking policy.


### Public startup of an isolated child desktop

With the explicit runtime enabled, `karyo-desktops switch NAME` still performs
local pkexec authorization, then sends only a versioned switch operation and a
validated account name to the root-only host. The host independently captures
and revalidates the peer's native process, login scope and account. A running
family session uses the existing lock-before-switch path. A stopped child gets
an owned private network, physical-underlay forwarding, private DNS, scoped user
manager and fresh controlled graphical PAM session, in that order.

The host registers the complete session before activation and supervises its
lifetime without a client connection. Loss of authorization before allocation
creates nothing; a failure after native mutation retains exact journal receipts
for recovery or administrator review. A partial startup is not silently retried.
After observed logout/crash the existing lifecycle path returns to Home and
closes GUI/manager, DNS, forwarding and network in ownership order.

A stopped main/Home session still belongs to the login manager and is not
started inside a private child namespace. Current public cold startup does not
claim automatic DHCP/DNS refresh, interactive VPN enrollment or half-created graphical
session recovery. These remain migration work rather than hidden success paths.


### NetBird provider migration boundary

A configured `VPN=netbird` child now starts its own journaled provider before
its graphical session through the public desktop switcher. Trusted optional CLI,
GUI, scoped wrappers, opener and catalog must exist before any allocation; missing
payload reports the required packages instead of starting ordinary networking.
The pre-login provider primitive owns a separate root-only state directory, private runtime socket and
transient native service inside the profile network/resolver context. Persistent
state is stamped with the complete account identity and captured home device/inode, and never recursively adopted,
chowned or deleted on a retry. It connects to the profile's private resolve1 bus,
never the host NetworkManager/system bus, and grants no SYS_ADMIN/SYS_MODULE capability.

Public activation uses the same provider/session ownership lifecycle as recovery.
Schema-5 and later host receipts capture its persistent state/home generation, native service invocation,
process start/executable/namespace, runtime socket and published client endpoint.
After a controller crash an offline pre-login provider can be verified and closed
before private DNS, forwarding and network cleanup. A lost stop acknowledgement
is recoverable only when the captured process and runtime objects are gone; an
unacknowledged state/service creation is retained for review. Credentials are not
serialized, adopted or removed. A graphical owner or changed namespace consumer
prevents this offline cleanup. Earlier journal schemas remain readable.

Connected tunnel ownership, graphical/client context and teardown remain journaled.
First enrollment is initiated by the ordinary scoped client; startup neither imports
credentials nor waits for interactive authentication while holding the host lock.
Browser PKCE, Home-tunnel interaction and remaining provider UI acceptance retain
their separate validation boundaries.
The external NetBird package's maintainer script starts a system-wide service;
installing that dependency must be an explicit packaging/administrator decision,
not a side effect of the base DE or this primitive. The DE neither replaces that
upstream package nor disables/adopts an administrator's existing global service.


### Isolated NetBird clients and browser handoff

The optional `profile-netbird-client` component installs `karyo-netbird-cli`,
`karyo-netbird-gui` and a private `xdg-open` handoff shim. It starts no service and
pulls in no vendor package that could enable a global daemon. The native CLI/GUI
binaries must be supplied separately through reviewed provider packaging.

After a verified provider start the root controller publishes `client.json` in
its private runtime directory. `KARYO_NETBIRD_ENDPOINT` selects that exact metadata
path; the ordinary client verifies root ownership, account/group, native network/resolver mount,
socket inode and root peer PID before using its derived daemon address. A caller
outside that profile context is refused. This prevents accidental wrong-desktop
launches; filesystem/socket permissions and the upstream daemon's caller checks
remain the access-control boundaries. No root state or credential is exposed.

Both clients clear inheritable/ambient capabilities with setpriv. They preserve
the ordinary user's capability bounding set and ability to use native sandboxing;
the GUI explicitly sets `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=0` so the vendor
bootstrap does not mistake Ubuntu userns restrictions for a reason to disable its
working renderer sandbox. Only the provider client
PATH includes the asynchronous opener shim. It accepts one HTTP(S) URL and hands
it to the native browser without waiting, so the provider can start its callback
listener even when the browser was cold. URL/code values are not logged on failure.
The public VPN startup path uses the captured provider/tunnel journal; actual
browser enrollment retains a separate acceptance boundary.


### Captured provider and graphical owner

An acknowledged pre-login NetBird provider can now precede `JournalSession`
startup. The captured session reattaches that exact service and revalidates it
around namespace consumer inventory; only its verified main PID is accepted
outside the captured resolver, graphical and user-manager scopes. An unknown
root process is not accepted merely because a provider exists.

The scoped native user manager and graphical entry receive the same derived
`KARYO_NETBIRD_ENDPOINT` environment. Plain profiles receive no provider setting.
Restore reconstructs the exact manager policy from the acknowledged provider
record. Login/manager closure completes before provider shutdown, which precedes
DNS, forwarding and network cleanup. Offline prepared/closing session recovery
can observe that provider while still refusing any unknown consumer.

This joins the provider to the graphical owner's lifetime in public startup and
recovery. The tunnel declaration below and schema7 catalog record extend that
ownership boundary. Optional inert vendor payload is built separately by Builder;
interactive enrollment and Home-tunnel interaction keep their own validation gates.


### Delegated virtual tunnel contents of an owned namespace

Before starting a journaled NetBird service, schema6 explicitly records a fixed
`tunnel` contents policy on its already captured private network. This allows one
namespace-local kernel WireGuard or userspace TUN device, with any native valid
interface name. It does not extend ownership to host VPN interfaces/routes,
physical devices, TAP/bridge/veth devices, masters or additional tunnel devices.
The original host/guest veth identity, alias, peer indices and namespace inode
remain checked. A plain profile retains its strict lo/eth0 inventory.

Routes, rules and firewall tables created inside that captured namespace are
namespace-local runtime state. After the graphical owner, natural manager,
provider and resolver have exited, cleanup removes the proven host veth and the
captured process-free namespace. It never sweeps tunnel names or global VPN
routes/firewall. Unknown devices or consumers retain the lease for review.
The declaration persists after provider teardown, so interrupted DNS/network
cleanup can still reconstruct the same ownership boundary. Schemas1–5 remain
readable with their original strict device policy; older connected receipts are
not silently promoted or adopted.

This boundary permits connected-provider lifetime checks. Native connected
teardown/recovery, peer traffic, DNS and public activation have their own gates;
this declaration alone is not proof of those behaviors.


### Captured provider catalog

New journaled NetBird desktops publish the fixed packaged catalog at
`/usr/share/karyo/profile-catalog/netbird` through the same `XDG_DATA_DIRS` policy
in their native user manager and graphical owner. The default system data roots
remain available. Plain desktops receive no provider catalog. The client
component owns the provider desktop identity there, with a scoped GUI executable;
no upstream global launcher, daemon or polkit action is installed by this layer.
GUI launch retains the upstream DMABUF-renderer default and the strict sandbox.

Schema7 records only the fixed `session.catalog = "netbird"` discriminator before
manager preparation; it requires a captured provider and accepts no free-form
path or command. Older session records without that field restore their exact
legacy manager policy, without adding the new environment or rewriting files.
Prepared, live and closing recovery use the recorded variant. Journal promotion
never downgrades after catalog/session removal. Packaged root-owned catalog data
must exist before a new catalog-enabled manager is prepared.

The inert optional provider package is built separately by Karyo Builder. Public
VPN startup requires this trusted payload and explicit administrator runtime
activation; catalog publication is not browser enrollment or complete migration.


### Real browser enrollment acceptance

The public NetBird startup path has installed actual S256 browser enrollment
acceptance on Debian with cold Chromium and Ubuntu with cold MiniBrowser/WebKit.
The fixed disposable QA child selects its ordinary HTTP/HTTPS XDG browser
association; the final gate uses the normal Karyo environment without a BROWSER
override. The asynchronous installed opener permits the private callback to
start, the real browser displays successful login, and the ordinary scoped CLI
and provider reach Connected before supervised logout returns Home.

The gate retains real renderer NNP/seccomp, a distinct user namespace and zero
ambient/inheritable capabilities. Browser automation tools and temporary owner
credentials belong only to disposable QA, not the DE package or user profiles.
This is distinct from the earlier synthetic callback and setup-key checks.
Onward provider GUI/tray behavior, arbitrary external identity providers and
Home-tunnel interaction retain their own validation boundaries.


### Optional Home/Work application visibility

The ordinary karyo-application-catalog helper reads administrator-owned
/etc/karyo/application-roles.json only when that explicit policy is installed.
Its version1 maps applications and autostart desktop-file basenames separately
to home/work role lists. Empty role lists disable that entry in both roles;
listing both roles leaves a shared application's original launcher untouched.
profiles/examples/application-roles.json illustrates Home-only Forge/example tunnel,
Work-only NetBird/Mattermost and shared Bitwarden. It is not installed or enabled
by the account package and contains no corporate endpoint or profile data.

The role is derived from real-UID account metadata, not USER or a caller flag:
WORKSTATION=yes selects Work; the other accounts of its family select Home.
Unrelated families stay unchanged. After initial PAM succeeds, session autostart
creates a private content-addressed runtime view containing only standard
Hidden=true masks. The existing application binaries, launch commands, icons,
licenses and user preferences stay at their original locations. The helper
performs no privileged mutation, package install or global service operation.

The session prepends its ordinary view to XDG_DATA_DIRS and XDG_CONFIG_DIRS and
publishes these values to its captured user manager/D-Bus activation before
Files, normal autostart or the shell run. Standard user-level XDG overrides keep
their precedence. Application visibility and autostart selection are desktop
preferences; the separate account/network/PAM boundaries enforce isolation.

Identical views reuse verified ordinary-user cache bytes/inodes; changed policy
produces a new atomic no-replace view. A mismatched/unsafe cache is retained and
reported. Runtime views expire with the user's native runtime directory. Missing
policy produces no view; invalid opted-in metadata stops session clients rather
than running a partially configured application catalog.

The fixed disposable-host installed-application-roles.py gate accepts the
ordinary Home resolver/cache and a fresh public Work graphical session on
Debian 13 and Ubuntu 26.04. It verifies native GLib visibility, actual allowed
and suppressed autostart, identical shell/Files/activation environments, running
session reuse, logout cleanup and the preserved original Home. It uses safe
QA desktop files rather than installing corporate clients or Forge. A fresh
Home graphical login and real vendor application acceptance remain separate.

The installed-cold-home-roles.py fixed-host gate now also accepts fresh Home
through actual LightDM on Debian 13 and Ubuntu 26.04. It ends the previous native
user manager/runtime, verifies Home/common autostart and shell/Files/activation
catalogs on a new compositor, then removes the owned policy and logs in cold
again. No role cache or catalog environment survives that second native login.
Temporary QA autologin and role entries are removed; account/PAM bytes and the
original display-manager configuration are restored. This is an unprotected
Home startup/lifecycle gate; interactive protected entry has separate acceptance.
