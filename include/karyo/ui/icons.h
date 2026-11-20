// SPDX-License-Identifier: GPL-3.0-or-later
// Lucide glyphs from the design's icon set, tinted like CSS currentColor.
#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>

namespace Karyo::Ui {

// Default stroke width of the Karyo system (Organic: 2.75).
constexpr qreal IconStroke = 2.75;

bool hasIcon(const QString &name);
// size is in device-independent pixels; the pixmap carries devicePixelRatio.
QPixmap icon(const QString &name, int size, const QColor &color, qreal devicePixelRatio = 1.0,
             qreal strokeWidth = IconStroke);

} // namespace Karyo::Ui
