// SPDX-License-Identifier: GPL-3.0-or-later
// Places a top-level widget as a wlr-layer-shell surface (via LayerShellQt).
#pragma once

#include <QFlags>

class QMargins;
class QScreen;
class QWidget;

namespace LayerSurface {

enum Layer { LayerBackground, LayerBottom, LayerTop, LayerOverlay };
enum Anchor { AnchorTop = 1, AnchorBottom = 2, AnchorLeft = 4, AnchorRight = 8 };
Q_DECLARE_FLAGS(Anchors, Anchor)

// A Qt placeholder during VT/output loss has no native wl_output.
bool outputAvailable(QScreen *screen);

// Must run before the widget is first shown. exclusiveZone -1 ignores other zones.
void configure(QWidget *widget, QScreen *screen, Layer layer, Anchors anchors, int exclusiveZone,
               bool keyboardOnDemand = false);
// Distance from the anchored edges; call after configure().
void setMargins(QWidget *widget, const QMargins &margins);
// Exclusive takes the keyboard while mapped; OnDemand keeps focus until the
// user clicks another surface.
enum Keyboard { KeyboardNone, KeyboardOnDemand, KeyboardExclusive };
void setKeyboard(QWidget *widget, Keyboard keyboard);

} // namespace LayerSurface

Q_DECLARE_OPERATORS_FOR_FLAGS(LayerSurface::Anchors)
