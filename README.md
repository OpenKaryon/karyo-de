# Karyo DE

Karyo DE is a Qt 6 desktop that can run independently on Debian 13 alongside
an existing desktop. This repository contains the UI SDK, Qt style, Wayland
shell, session launcher and PAM/Wayland lock. Karyo Linux consumes the same
components and supplies its own OS policies separately.

The foundation provides the existing light/dark palettes, accent choices,
IBM Plex font preferences and painting helpers. It reads the existing
`openkaryon/desktop.conf` user settings for compatibility. Its static library
does not install a display manager, session, fonts, browser defaults or services.

## Build and test

On Debian 13, build dependencies are CMake 3.21+, Ninja, a C++17 compiler,
`qt6-base-dev`, `qt6-base-dev-tools`, `qt6-svg-dev`, `qt6-wayland-dev`,
`qt6-wayland-private-dev`, `qt6-base-private-dev`, `qt6-l10n-tools`,
`libpam0g-dev`, `liblayershellqtinterface-dev` and `libpolkit-qt6-1-dev`.
Use Karyo Builder's Debian container for the recorded environment.
To build only the UI SDK, set `-DKARYO_BUILD_SESSION=OFF`.

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
and [session](docs/SESSION.md) for installation boundaries.

The implementation is adapted from the Karyo desktop prototype. See
[provenance](docs/PROVENANCE.md) and the [migration map](docs/MIGRATION.md).
Own code is GPL-3.0-or-later. The embedded Lucide resources retain their
[upstream license](resources/lucide/LICENSE). Qt and other dependencies retain
their upstream licenses. The optional font runtime bundles unmodified IBM Plex under its original SIL OFL;
see [font provenance](resources/fonts/README.md).

## Next milestone

The independent session passes a real headless labwc/PAM scenario. Complete
actual display-manager login/coexistence and hardware suspend, GPU and
multiple-output acceptance before declaring the DE release-ready.

## Standalone session

The shell and PAM/Wayland lock are now independently buildable with
`-DKARYO_BUILD_SESSION=ON` (the default). Debian build dependencies and package
recipes live in Karyo Builder. The `karyo-session` bundle installs the shell,
lock, style and Wayland entry alongside an existing display manager.
See [session boundaries and acceptance](docs/SESSION.md).

The optional greetd front end is built with `-DKARYO_BUILD_GREETER=ON`. Its
separate package does not activate a display manager or replace PAM policy. See
[greeter integration](docs/GREETER.md) before explicit administrator setup.
