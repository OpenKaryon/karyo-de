# Physical connection controls

The host keeps Wi-Fi/Ethernet while managed desktops select their private
NetworkManager for VPNs. `org.karyo.PhysicalNetwork` is a separate, narrow system
bus service for the shared physical connection. It exposes versioned JSON state
and radio, saved-connection activation/disconnection and personal Wi-Fi actions.
It has no command, namespace, resolver, routing or host-VPN operation.
Existing Ethernet configured outside NetworkManager is reported read-only from
its physical device and default route. The mediator does not take over or rewrite
an administrator's existing network configuration.

Inventory includes only NetworkManager Ethernet/Wi-Fi devices with a hardware
parent in sysfs. Software links and VPN profiles are excluded. Saved profiles
are checked for a simple physical connection type; new Wi-Fi uses the current
device's access-point bytes and security flags. Device identity, NetworkManager
bus owner and selected profile/access point are checked again after authorization.
The UI returns the snapshot's NetworkManager owner with its selection, so a
manager restart requires refreshing the list rather than reusing object paths.
The service never calls GetSecrets or exports stored credentials.

The system bus supplies the original application's unique name, UID and PID.
The service checks its process lifetime and one active, unlocked local graphical
login for that UID, including ordinary user-manager applications. Every mutation
asks the distribution polkit authority about that original `system-bus-name`
subject, using the existing NetworkManager action IDs. No proxy UID or caller-
provided identity is used. New saved Wi-Fi also requests the normal system-profile
modification action. Administrator policy and normal challenge dialogs apply.

The root service talks to the unchanged host NetworkManager over D-Bus and needs
no network-administration or process-tracing capability.
Its startup verifies its fixed systemd cgroup, MainPID and no-capability policy
on the real system bus, rather than reading another process's namespace files.
Unlike the raw network controller, this mediator performs no namespace or kernel
network mutation itself. Installation supplies
standard D-Bus activation metadata and an inactive systemd unit; it does not
enable isolated networking, alter a physical profile, stop a VPN or start a
desktop. Python D-Bus and GLib are runtime dependencies of the optional package.

Activation is asynchronous to the GUI. The service reports completion, failed
Wi-Fi authentication, authorization failure or a bounded connection timeout.
Open, personal WPA/WPA2/WPA3 and OWE access points use the existing NM API;
enterprise/WEP setup is not invented by this password form. Authentication and
activation recheck the active caller before proceeding. The ordinary NetworkManager
and polkit interfaces are documented in the [NM API](https://networkmanager.dev/docs/api/latest/)
and [polkit authority API](https://www.freedesktop.org/software/polkit/docs/latest/eggdbus-interface-org.freedesktop.PolicyKit1.Authority.html).

Functional models pass on Debian and Ubuntu for physical inventory, saved
Ethernet, personal Wi-Fi plans, radio changes and activation/error states. The
installed mediator also passes an actual system-bus inventory call from the
ordinary Home user manager on both native bases. It autoactivates without
capabilities, retaining the original caller identity and desktop receipts.

Shell and Settings share one asynchronous physical-connection panel. Native Qt
D-Bus tests cover activation, short passwords, failed-password retry, external
Ethernet and a service update mismatch. Both applications compile on Debian
Qt 6.8 and Ubuntu Qt 6.10; installed Settings pages were captured from real Home
Wayland sessions. The QA Ethernet is managed by networkd, so the panel reports
it read-only and keeps private desktop connections separate. The normal session
autostart loop replaces an outdated Shell without retiring either desktop.
Post-upgrade Home/Work checks again pass managed DNS, verified external HTTPS
and encrypted peer TCP/UDP on both bases.

Real Wi-Fi radio, association, hardware password entry and a normal physical
connection authorization challenge are not claimed by this Ethernet-only gate.
The laboratory has no hardware Wi-Fi adapter. Physical DNS/DHCP coordination has
its own [installed family result](VALIDATION_INSTALLED_FAMILY_NETWORKS.md).
