// SPDX-License-Identifier: GPL-3.0-or-later
// How an app is drawn on the taskbar: a colored 36px tile with a Lucide glyph
// for the core apps of the design, the app's own icon otherwise.
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class DesktopEntry;
class QPainter;
class QRectF;

struct AppStyle
{
    QString glyph;      // Lucide icon name; empty if none
    QString monogram;   // one letter (apps) or a short mono name (CLI tools) instead of a glyph
    QColor tile;        // tile background; invalid means no tile
    QColor ink;         // glyph or monogram color
    QIcon themeIcon;    // used when there is no glyph and no monogram

    static AppStyle forEntry(const DesktopEntry &entry);
    static AppStyle fallback(const QString &name);

    // Draws the app tile into a square: the design's 36px tile (radius 11,
    // 20px glyph) scaled to the square's size.
    void paint(QPainter &painter, const QRectF &tile, qreal devicePixelRatio) const;
};
