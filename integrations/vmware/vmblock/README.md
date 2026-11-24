# Optional VMware shutdown release

This independent system component releases an armed VMware vmblock FUSE
connection before logind waits for session processes during shutdown. A blocked
file-transfer request can otherwise keep vmusr in uninterruptible kernel sleep
after its process group receives a termination signal.

The service is restricted to VMware. Session scope ordering stops it before
session teardown, while its dependencies retain the FUSE control filesystem
and native tools until after its stop action. The helper requires a queued
shutdown target: an ordinary service restart, package removal or logout does
not abort transfers. Shutdown can cancel an in-flight transfer.

Only the exact native vmblock mount type/source is accepted. The helper reads
mount metadata without accessing the stalled mount, pins its existing FUSE
control directory, rechecks the captured mount and writes to its existing
abort node. HGFS and unrelated FUSE connections are not selected.

Installation is explicit and separate from the ordinary-user session
coordinator. The package adds no user, changes no PAM/display manager and does
not reset transfers during ordinary desktop use. The systemd stop timeout is
eight seconds; the native job query has its own three-second bound.

The behavior is adapted from reviewed prototype revision `8baf9c1c` under
GPL-3.0-or-later. Native source/package and isolated FUSE/shutdown acceptance
are recorded separately; unit checks alone do not prove guest reboot behavior.
