# Optional desktop profile integration

This is the first reviewed policy slice of the prototype's independent desktop
sessions. It is not installed or activated yet. Account mutation, PAM password
synchronization, logind/VT switching, network namespaces, per-profile VPN/DNS
and family clipboard services follow as separately reviewed runtime slices.

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
