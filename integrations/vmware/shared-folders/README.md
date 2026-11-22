# Optional system VMware shared folders

This separate package restores the prototype's system HGFS mount at /mnt/hgfs.
It uses the native open-vm-tools vmhgfs-fuse and vmware-hgfsclient programs, not
a GUI session's elevated worker. Install it only when host shared folders are
wanted. It leaves the ordinary VMware session/clipboard component independent.

The enabled oneshot system service runs only inside VMware. Disabled host
sharing skips the service instead of leaving a disconnected FUSE mount. If
sharing is enabled later, an administrator can restart karyo-vmhgfs.service.
An existing enabled-but-empty share root can show subsequently added folders.
The host remains the authority for which folders are exposed. Guest users with
access can modify writable shares with the host's configured permissions.

Mounts use nosuid,nodev,allow_other. The root helper accepts only start/stop for
the fixed target, checks owned nonsymlink directories and private state, and
serializes invocations. Existing vendor/admin mounts are left alone. Shutdown
unmounts only the recorded unique mount ID/device/source, leaving disappeared/replaced
mounts and user files intact. Removal retains /mnt/hgfs as an ordinary directory.
Package lifecycle preserves the administrator's disabled service state.

System mount operations intentionally remain in the system mount namespace;
ProtectSystem/PrivateTmp filesystem sandboxes would hide the mount from users.
NoNewPrivileges is retained. This service changes no users, PAM, display
manager, networking or root vmtools daemon. Its install scripts are declared
explicitly in Builder's optional recipe and are never run during package builds.

The source-owned tests/installed-service.py actor and Builder driver now pass
the full native lifecycle on both target guests: actual bidirectional ordinary
file exchange, private-mount inheritance, NoNewPrivs1 on the root FUSE child,
foreign/same-type replacement preservation, administrator disable on reinstall,
and purge after host disconnection. Account/PAM/DM/fstab/shell and root vmtools
state remain unchanged. Thirteen source cases and both integrated VMware CTest
groups pass. These results do not establish clipboard, drag/drop, actual
Home/Work GUI handover or multi-monitor behavior. See Builder
VALIDATION-VMWARE-HGFS.md for exact package inputs, receipts and retained controls.


Mountinfo numbers and anonymous FUSE device numbers can be reused immediately.
The helper additionally binds ownership to the non-recycled 64-bit kernel mount
ID, queried without filesystem I/O through explicit numeric findmnt/listmount
columns. It requires util-linux with that backend and a supporting kernel
(Linux6.8+; verified on both native target kernels). Unsupported identity queries
fail before creating a new mount. No persistent Python daemon or elevated GUI
worker is added. See the primary [findmnt documentation](https://man7.org/linux/man-pages/man8/findmnt.8.html)
and [statmount API](https://man7.org/linux/man-pages/man2/statmount.2.html).
