// SPDX-License-Identifier: GPL-3.0-or-later
#include "openkaryon/ui/paint.h"

#include "openkaryon/ui/theme.h"

#include <QHash>
#include <QImage>
#include <QPainterPath>

#include <cmath>

namespace OpenKaryon::Ui {

namespace {

// Three box-blur passes approximate a Gaussian of the given sigma (alpha channel only).
void blurAlpha(QImage &image, qreal sigma)
{
    if (sigma <= 0.0)
        return;
    const int boxes = 3;
    const qreal ideal = std::sqrt(12.0 * sigma * sigma / boxes + 1.0);
    int lower = int(std::floor(ideal));
    if (lower % 2 == 0)
        --lower;
    const int upper = lower + 2;
    const int m = int(std::round((12.0 * sigma * sigma - boxes * lower * lower - 4.0 * boxes * lower
                                  - 3.0 * boxes)
                                 / (-4.0 * lower - 4.0)));

    const int w = image.width();
    const int h = image.height();
    QVector<int> line(qMax(w, h));
    auto pass = [&](int radius, bool horizontal) {
        const int outer = horizontal ? h : w;
        const int inner = horizontal ? w : h;
        for (int o = 0; o < outer; ++o) {
            auto at = [&](int i) -> uchar & {
                return horizontal ? image.scanLine(o)[i * 4 + 3] : image.scanLine(i)[o * 4 + 3];
            };
            for (int i = 0; i < inner; ++i)
                line[i] = at(i);
            int sum = 0;
            const int span = radius * 2 + 1;
            for (int i = -radius - 1; i < radius; ++i)
                sum += (i >= 0 && i < inner) ? line[i] : 0;
            for (int i = 0; i < inner; ++i) {
                const int add = i + radius;
                const int drop = i - radius - 1;
                sum += (add < inner) ? line[add] : 0;
                sum -= (drop >= 0) ? line[drop] : 0;
                at(i) = uchar(sum / span);
            }
        }
    };
    for (int i = 0; i < boxes; ++i) {
        const int size = i < m ? lower : upper;
        const int radius = (size - 1) / 2;
        pass(radius, true);
        pass(radius, false);
    }
}

} // namespace

void fillPill(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal radius = qMin(rect.width(), rect.height()) / 2.0;
    fillRounded(painter, rect, radius, color);
}

void fillRounded(QPainter &painter, const QRectF &rect, qreal radius, const QColor &fill,
                 const QColor &border)
{
    if (fill.alpha() == 0 && border.alpha() == 0)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (border.alpha() != 0) {
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(rect, radius, radius);
    }
    painter.restore();
}

void drawShadow(QPainter &painter, const QRectF &rect, qreal radius, QPointF offset, qreal blur,
                const QColor &color)
{
    // CSS blur radius is twice the Gaussian sigma.
    const qreal sigma = blur / 2.0;
    const int pad = int(std::ceil(sigma * 3.0));
    const QSize size(int(std::ceil(rect.width())) + pad * 2, int(std::ceil(rect.height())) + pad * 2);

    static QHash<QString, QImage> cache;
    const QString key = QStringLiteral("%1x%2/%3/%4/%5")
                            .arg(size.width())
                            .arg(size.height())
                            .arg(radius)
                            .arg(blur)
                            .arg(color.name(QColor::HexArgb));
    QImage image = cache.value(key);
    if (image.isNull()) {
        image = QImage(size, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        {
            QPainter p(&image);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::black);
            p.drawRoundedRect(QRectF(pad, pad, rect.width(), rect.height()), radius, radius);
        }
        blurAlpha(image, sigma);
        // Recolor: keep the blurred alpha, apply the shadow color.
        for (int y = 0; y < image.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const int a = qAlpha(row[x]) * color.alpha() / 255;
                row[x] = qRgba(color.red(), color.green(), color.blue(), a);
            }
        }
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (cache.size() > 32)
            cache.clear();
        cache.insert(key, image);
    }

    // Like CSS, the shadow is not painted under the box itself.
    painter.save();
    QPainterPath outside;
    outside.addRect(QRectF(rect.topLeft() + offset - QPointF(pad, pad), QSizeF(size)));
    QPainterPath box;
    box.addRoundedRect(rect, radius, radius);
    painter.setClipPath(outside.subtracted(box));
    painter.drawImage(rect.topLeft() + offset - QPointF(pad, pad), image);
    painter.restore();
}

void drawLargeShadow(QPainter &painter, const QRectF &rect, qreal radius)
{
    const bool dark = Theme::current().scheme == ColorScheme::Dark;
    drawShadow(painter, rect, radius, QPointF(0, 12), 32,
               dark ? alpha(Qt::black, 50) : alpha(neutral(900), 22));
}

QMarginsF largeShadowMargins()
{
    // sigma 16 * 3, plus the 12px downward offset.
    return QMarginsF(48, 36, 48, 60);
}

void drawKaryoMark(QPainter &painter, const QRectF &rect)
{
    // Proportions of the 32px mark in KaryonShell.dc.html.
    const qreal s = rect.width() / 32.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::current().accent);
    painter.drawEllipse(rect);
    painter.setBrush(neutral(100));
    painter.drawEllipse(QRectF(rect.right() - (4 + 15) * s, rect.top() + 5 * s, 15 * s, 15 * s));
    painter.setBrush(sage(600));
    painter.drawEllipse(QRectF(rect.right() - (9 + 5) * s, rect.top() + 10 * s, 5 * s, 5 * s));
    painter.restore();
}

} // namespace OpenKaryon::Ui
