# DE migration boundary

The approved [per-desktop networking plan](NETWORK_ISOLATION.md) is implemented
for Home and child desktops, with separately owned network, DNS and service
contexts. Unchanged upstream VPNs use owned launch adapters; the optional worker
producer adds scoped application integration without replacing those protocols.
The [installed IPv4 matrix](VALIDATION_INSTALLED_FAMILY_NETWORKS.md) accepts all
nine ordered Home/Work pairs of WireGuard, OpenVPN and NetBird on Debian and
Ubuntu. IPv6 remains disabled; physical Wi-Fi and a different interface/gateway
retain their explicitly documented limits.

The general application launcher now preserves the actual desktop service view
in independent application scopes. Native menu-to-worker activation and
packet-engine payloads, separate Work DNS, cancellation, recovery and cleanup
have their own installed acceptance. See [worker integration](DESKTOP_WORKERS.md)
and Builder's `docs/VALIDATION_DESKTOP_WORKERS.md` for the observed boundaries.

The [final joined acceptance](https://github.com/OpenKaryon/karyo-builder/blob/main/docs/VALIDATION_FINAL_JOINED.md)
now covers 42 built packages per native base, core desktop coexistence,
optional runtime upgrade and clean package removal. Protected Experiments
requires the Home password on first and repeated entry, including after a real
populated reboot. Ordinary external GUI import/enrollment, native authorization,
connect/disconnect/reconnect and independent Home/Work traffic also pass on
Debian and Ubuntu. Dedicated test credentials remain outside public inputs and
are removed from the QA guests after acceptance.

Actual VMware Windows-host clipboard, DnD and display events remain an external
acceptance gate because Windows Computer Use cannot initialize. Physical
hardware/carrier and IPv6 limitations remain explicit in the linked reports.
Distribution images and Forge are outside this DE/Builder stage; existing
compatibility engine boundaries remain frozen.

The first migrated component is the shared theme/paint foundation. It is
independently buildable and installable as development files, with an exported
CMake target, the `Karyo::Ui` namespace and compatible user settings keys.

Prototype source is reviewed for API compatibility, dependencies, system
effects, provenance and regression behavior before migration. Working code is
reused and adapted or rewritten where needed, following CONTRIBUTING.md.
The first component's precise origin is recorded in PROVENANCE.md.

Theme/paint, desktop entries/icons, common widgets and the Qt style are now
separate exported components. The shell, PAM/Wayland lock and session entry
now build independently. File-operation/type primitives and Wi-Fi behavior
belong to the shell boundary; the optional app catalog is XDG data. The
optional greeter, system integrations and distribution defaults remain separate.
See SESSION.md for verified behavior and remaining acceptance.
The complete source-to-product inventory is maintained in Karyo Builder's
`docs/MIGRATION.md`.

The standalone session acceptance milestone is installation on clean Debian,
session selection alongside an existing desktop, upgrade, removal and
preservation of user data and existing system settings.
