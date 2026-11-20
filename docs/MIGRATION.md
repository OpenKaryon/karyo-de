# DE migration boundary

The first migrated component is the shared theme/paint foundation. It is
independently buildable and installable as development files, with an exported
CMake target and the original C++ namespace/settings keys.

Prototype source is reviewed for API compatibility, dependencies, system
effects, provenance and regression behavior before migration. Working code is
reused and adapted or rewritten where needed, following CONTRIBUTING.md.
The first component's precise origin is recorded in PROVENANCE.md.

Shared widgets/icons/platform integration follow in separate changes. Then
separate lock/session packaging from greeter and distribution defaults.
The complete source-to-product inventory is maintained in Karyo Builder's
`docs/MIGRATION.md`.

The standalone session acceptance milestone is installation on clean Debian,
session selection alongside an existing desktop, upgrade, removal and
preservation of user data and existing system settings.