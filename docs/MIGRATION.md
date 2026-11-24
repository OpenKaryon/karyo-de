# DE migration boundary

The approved [per-desktop networking plan](NETWORK_ISOLATION.md) adds a private
network and DNS context for Home as well as child desktops. Existing upstream
VPNs use owned launch adapters; a bespoke provider API is optional. The current
runtime has not yet implemented this main-session topology. The linked plan
defines the implementation order and installed Debian/Ubuntu acceptance gates.

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
