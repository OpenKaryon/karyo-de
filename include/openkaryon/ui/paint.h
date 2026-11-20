// SPDX-License-Identifier: GPL-3.0-or-later
// Painting helpers for the rounded, soft Organic shapes.
#pragma once

#include <QColor>
#include <QPainter>
#include <QRectF>

namespace OpenKaryon::Ui {

// border-radius: 999px.
void fillPill(QPainter &painter, const QRectF &rect, const QColor &color);
void fillRounded(QPainter &painter, const QRectF &rect, qreal radius, const QColor &fill,
                 const QColor &border = Qt::transparent);
// CSS box-shadow: offsetX offsetY blur color, cast by a rounded rect.
void drawShadow(QPainter &painter, const QRectF &rect, qreal radius, QPointF offset, qreal blur,
                const QColor &color);
// --shadow-lg: 0 12px 32px neutral-900 @ 22% (light) or black @ 50% (dark).
void drawLargeShadow(QPainter &painter, const QRectF &rect, qreal radius);
// Space a widget must keep around a rect for drawLargeShadow.
QMarginsF largeShadowMargins();

// The Karyo mark: accent circle, neutral-100 nucleus and a sage dot (logo placeholder).
void drawKaryoMark(QPainter &painter, const QRectF &rect);

} // namespace OpenKaryon::Ui
