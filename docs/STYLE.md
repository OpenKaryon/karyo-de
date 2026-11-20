# Karyo Qt style and optional client frames

The style, client-frame implementation and Qt plugin are adapted from the
Karyo prototype at revision `716d515b`. Own code is GPL-3.0-or-later.
Embedded Lucide resources retain their original attribution.

Applications can find `KaryoStyle`, link `Karyo::Style` and call
`Karyo::Ui::applyStyle()`. The development install component is
`style-development`. Other Qt 6 clients can select the installed plugin
with `QT_STYLE_OVERRIDE=Karyo`; its component is `style-runtime`.

The plugin uses the distribution's native Qt plugin directory. It installs no
global environment override or display-manager configuration. It changes only
applications that select the style. Platform dialogs/portals remain a separate
integration boundary.

Client-side frames are optional for external applications. Set
`KARYO_CLIENT_FRAME=1` for applications whose compositor rule disables server
decorations. The legacy OPENKARYON_CLIENT_FRAME variable remains a fallback;
the new setting takes precedence. KaryoWindow already supplies its own frame.
Menus, popup windows and already-frameless windows are excluded.

The plugin tests load Karyo through QStyleFactory, render normal controls,
check slider-handle bounds and exercise frame opt-in in separate processes.
The runtime plugin is packaged with automatic ELF dependencies by Builder.
