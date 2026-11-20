# UI foundation provenance

The four theme/paint headers and implementations were reviewed and adapted
from the Karyo desktop prototype at revision
`716d515b765319981b124fb1812a1e3edb9e0ffd`, under
`distro/desktop/shared/libopenkaryon-ui/`.

The public component retains existing color calculations, accent choices,
settings keys, font fallbacks and drawing behavior. Public headers and the
C++ namespace are renamed to `karyo/ui` and `Karyo::Ui`.
The new build boundary exposes an installed CMake target and static development
package, independent of the original monorepository build and OS defaults.
Tests cover the retained behavior and an independent installed consumer.

Copyright (C) 2026 Karyo contributors.
These files and the new build/test code are licensed GPL-3.0-or-later.

Qt is an external dependency and retains its upstream licensing. Font family
names are preferences only. No third-party fonts, icon resources, wallpapers,
private endpoints, machine configuration or private Git history are imported.
## Subsequent reviewed components

The same donor revision supplies desktop entries/icons, window, navigation,
password, menu and tooltip widgets, plus the style/client-frame sources and
headers under distro/desktop/shared/libopenkaryon-ui. The Qt plugin is adapted
from distro/desktop/shared/style-plugin. These are now independent public
CMake components with installed-consumer and regression checks.

Lucide resources are imported separately and retain the license under
resources/lucide/LICENSE. The foundation-only statement above describes
the original slice; desktop integration and the runtime plugin now embed
those attributed icons. No fonts or machine configuration are embedded.

Session keyboard/renderer behavior and labwc configuration are adapted from
distro/distro/packages/openkaryon-shell. The public launcher adds explicit
backend preflight checks, scoped session identity, relocatable configured
installation paths and a separate preparatory package. The complete shell,
PAM lock, optional greeter and privileged system integrations are not included
in this preparation. See SESSION.md for the remaining acceptance gates.
