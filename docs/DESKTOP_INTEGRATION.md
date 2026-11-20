# Desktop entries and icon integration

This component reuses the Karyo prototype's desktop-entry reader, application
identity matching and application icon rendering from revision
`716d515b765319981b124fb1812a1e3edb9e0ffd`, under
`distro/desktop/shared/libopenkaryon-ui`.

Public includes are `karyo/ui/{desktopentry,appstyle,icons}.h`.
Use `find_package(KaryoDesktopIntegration CONFIG REQUIRED)` and link
`Karyo::DesktopIntegration`. It depends on the UI foundation and Qt SVG.

Retained behavior includes exact desktop/StartupWMClass matching, Chromium/
Electron profile suffixes, unique qualified identifiers such as Mattermost,
native theme/file icons, monogram fallback and the existing core application
roles. Executable/desktop identifiers from the prototype remain supported.
Explicit launch methods use QProcess without a shell.

MIME defaults now respect all colon-separated current-desktop identifiers.
An unrelated desktop's defaults are not loaded merely because its files
happen to exist in the user's configuration.

The 137 embedded Lucide resources retain the donor's ISC/Feather attribution
in `resources/lucide/LICENSE`, also installed with the component.
Own code is GPL-3.0-or-later. Resource initialization is explicit so icons remain
available to consumers linking the installed static archive.

This library does not install a launcher, modify MIME defaults, change browser
preferences or provision services. Existing icon/matching regressions and an
independent installed consumer validate this boundary.
