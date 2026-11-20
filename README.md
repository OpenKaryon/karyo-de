# Karyo DE

Karyo DE is the Qt 6 desktop for Karyo Linux. This repository currently contains
the independently buildable UI foundation, desktop-entry/icon integration
and common widgets.
The Qt style/plugin is also extracted. A preparatory session launcher/configuration
is available; the shell and PAM/Wayland lock still need migration.

The foundation provides the existing light/dark palettes, accent choices,
IBM Plex font preferences and painting helpers. It reads the existing
`openkaryon/desktop.conf` user settings for compatibility. Its static library
does not install a display manager, session, fonts, browser defaults or services.

## Build and test

On Debian 13, build dependencies are CMake 3.21+, Ninja, a C++17 compiler,
`qt6-base-dev`, `qt6-base-dev-tools` and `qt6-svg-dev`. Install dependencies explicitly in
a development environment, or use the Karyo Builder container recipe.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
cmake --install build --prefix /usr --component development
```

Installed consumers use `find_package(KaryoUiFoundation 0.1 CONFIG REQUIRED)`
and link `Karyo::UiFoundation`. For desktop entries and icons, find
`KaryoDesktopIntegration` and link `Karyo::DesktopIntegration`.
Its install component is `desktop-development`. Common window, navigation,
password and menu controls use `KaryoWidgets` / `Karyo::Widgets` and the
`widgets-development` component. Public headers use `karyo/ui/*.h` and the
`Karyo::Ui` namespace. Existing user settings retain the prototype's
`openkaryon/desktop.conf` location. No ABI stability is promised before the
first DE release.
A downstream build smoke check is in `tests/consumer`.

The style development target is `Karyo::Style`; the independent runtime plugin
can be selected with `QT_STYLE_OVERRIDE=Karyo`. See [style](docs/STYLE.md)
and [session preparation](docs/SESSION.md) for installation boundaries.

The implementation is adapted from the Karyo desktop prototype. See
[provenance](docs/PROVENANCE.md) and the [migration map](docs/MIGRATION.md).
Own code is GPL-3.0-or-later. The embedded Lucide resources retain their
[upstream license](resources/lucide/LICENSE). Qt and other dependencies retain
their upstream licenses. IBM Plex is referenced by family name, not bundled.

## Next milestone

Migrate the shell with its file-operation/catalog dependencies, separate PAM lock
from the optional greeter, then validate a complete selectable session on clean
Debian. Verify login/lock, upgrade, removal and existing-desktop coexistence.
