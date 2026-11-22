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
unmounts only the recorded mount ID/device/source, leaving disappeared/replaced
mounts and user files intact. Removal retains /mnt/hgfs as an ordinary directory.
Package lifecycle preserves the administrator's disabled service state.

System mount operations intentionally remain in the system mount namespace;
ProtectSystem/PrivateTmp filesystem sandboxes would hide the mount from users.
NoNewPrivileges is retained. This service changes no users, PAM, display
manager, networking or root vmtools daemon. Its install scripts are declared
explicitly in Builder's optional recipe and are never run during package builds.

Real temporary HGFS exchange already succeeds in both native QA guests as the
ordinary account. Packaged service activation, ownership/foreign-mount handling,
disabled-host skipping and removal still need their separate installed gate.
These results do not establish clipboard, drag/drop or multi-monitor behavior.
