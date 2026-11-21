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
