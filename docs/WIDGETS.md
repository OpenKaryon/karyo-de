# Shared widgets

The shared widget component adapts the prototype's navigation, title tabs,
window controls, search/password fields, menus and tooltips from
`distro/desktop/shared/libopenkaryon-ui` at revision `716d515b`.

Consumers find `KaryoWidgets` and link `Karyo::Widgets`. Public headers use
`karyo/ui`. The static development package depends on desktop integration
and the UI foundation. The internal frame header is kept private.

Regression checks cover password submission during busy states, password
masking, tab selection after removal, hidden navigation rows, menu content,
and normal/maximized window corners. An installed downstream consumer creates
a real window containing navigation and password widgets.

The original Qt widget style and optional client frame are packaged separately.
This component adds no services, display manager or OS defaults.
