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

## Activating an existing file manager window

The common application helper now requests a standard xdg-activation token from
the focused Wayland caller and passes it in FileManager1's startup ID. A background
Files process cannot create authorization from another application's input serial.
This corrects the reproduced cross-process case where the folder tab was reused
but remained behind the requesting application on both native bases.

Each invocation owns its registry synchronization and token proxy. Qt dispatches
those callbacks asynchronously on its normal GUI queue; no blocking display
roundtrip or process-wide token environment mutation occurs in the caller.
A window-wide Qt token-created signal was deliberately avoided because it cannot
correlate concurrent invocations. Missing protocol/input falls back to an ordinary
request, a one-second deadline prevents indefinite waiting, and the optional
context owns cancellation. Files retains its native Qt token-consumption path.

Only the small QtGui native window-surface interface is private; opaque token
creation uses the standard MIT Wayland XML. Both Qt 6.8 and 6.10 compile and pass
native checks. Every independent caller runtime installs that protocol's original
copyright and full license under its own documentation path.

The BUILD_TESTING-only karyo-app-filemanager-fixture accepts a local folder. Open
that folder in Files, put the fixture in front and click Open folder: the existing
Files window must become active without another window. Before the fix this fails
on both native QA VMs; the fixed native fixture passes with installed Files.
Its --verify-tokens mode requires a private dbus-run-session on a disposable
Wayland display. A real user click issues two concurrent requests and destroys a
third request's context. It requires two distinct nonempty tokens for the right
paths, no cancelled request and no late duplicate after the timeout interval.
This mode passed on both installed QA sessions; it does not use a fake compositor.

The complete 67-entry CTest suite also passes on both native bases, including
ordinary offscreen routing/fallback and URI/selection regressions. These native
fixture results precede the caller runtime package wave; installed acceptance
of the rebuilt applications is recorded separately by Builder.

The eight caller runtime packages from exact activation source 82f9d4b are now
installed and accepted on both native QA VMs. Actual Photos menu activation
raises the existing Files window and selects its generated image fixture; PID
survival and integrity checks pass. This closes the installed caller gate left
open by the source-fixture checkpoint above.
