// SPDX-License-Identifier: GPL-3.0-or-later
#include "gallery.h"

#include "thumbnails.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

namespace Ui = Karyo::Ui;

namespace {

constexpr int Tile = 176;
constexpr int Gap = 8;
constexpr int HeadingHeight = 52;
constexpr int StripTile = 64;
constexpr int StripGap = 8;

void drawThumbnail(QPainter &painter, const QRect &rect, const QPixmap &pixmap, qreal radius)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect), radius, radius);
    painter.save();
    painter.setClipPath(clip);
    if (pixmap.isNull()) {
        painter.fillRect(rect, t.card);
        const int glyph = qMin(28, rect.width() / 3);
        painter.drawPixmap(rect.center().x() - glyph / 2, rect.center().y() - glyph / 2,
                           Ui::icon(QStringLiteral("image"), glyph, t.muted, painter.device()->devicePixelRatioF()));
    } else {
        painter.drawPixmap(rect, pixmap);
    }
    painter.restore();
}

} // namespace

// ---------------------------------------------------------------- GalleryGrid

GalleryGrid::GalleryGrid(Thumbnails *thumbnails, QWidget *parent)
    : QWidget(parent)
    , m_thumbnails(thumbnails)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    connect(thumbnails, &Thumbnails::ready, this, qOverload<>(&QWidget::update));
}

void GalleryGrid::setPictures(const QList<Picture> &pictures)
{
    m_pictures = pictures;
    m_items = layoutItems(width());
    m_hover = -1;
    updateGeometry();
    update();
}

QList<GalleryGrid::Item> GalleryGrid::layoutItems(int width) const
{
    QList<Item> items;
    // Tiles grow to fill the row: as many as fit at the base size.
    const int columns = qMax(1, (width + Gap) / (Tile + Gap));
    const int tile = qMax(Tile, (width - (columns - 1) * Gap) / columns);
    int y = 0;
    int column = 0;
    QString month;
    const QLocale locale;
    for (int i = 0; i < m_pictures.size(); ++i) {
        const QDate date = m_pictures.at(i).modified.date();
        // "October 2026", "Октябрь 2026": the month's standalone name.
        QString heading = locale.standaloneMonthName(date.month()) + QLatin1Char(' ') + QString::number(date.year());
        if (!heading.isEmpty())
            heading[0] = heading.at(0).toUpper();
        if (heading != month) {
            if (column > 0)
                y += tile + Gap;
            month = heading;
            column = 0;
            items.append({QRect(0, y, width, HeadingHeight), -1, heading});
            y += HeadingHeight;
        }
        items.append({QRect(column * (tile + Gap), y, tile, tile), i, QString()});
        if (++column == columns) {
            column = 0;
            y += tile + Gap;
        }
    }
    return items;
}

int GalleryGrid::heightForWidth(int width) const
{
    const QList<Item> items = layoutItems(width);
    return items.isEmpty() ? 0 : items.last().rect.bottom() + Gap;
}

QSize GalleryGrid::sizeHint() const { return {4 * (Tile + Gap), heightForWidth(4 * (Tile + Gap))}; }

void GalleryGrid::resizeEvent(QResizeEvent *) { m_items = layoutItems(width()); }

void GalleryGrid::paintEvent(QPaintEvent *event)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items.at(i);
        if (!item.rect.intersects(event->rect()))
            continue;
        if (item.picture < 0) {
            painter.setFont(Ui::headingFont(19));
            painter.setPen(t.text);
            painter.drawText(item.rect.adjusted(2, 0, 0, -8), Qt::AlignBottom | Qt::AlignLeft, item.heading);
            continue;
        }
        drawThumbnail(painter, item.rect, m_thumbnails->get(m_pictures.at(item.picture).path), 14);
        if (i == m_hover) {
            painter.setPen(QPen(t.accent, 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(item.rect).adjusted(1.5, 1.5, -1.5, -1.5), 13, 13);
        }
    }
}

int GalleryGrid::itemAt(const QPoint &pos) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).picture >= 0 && m_items.at(i).rect.contains(pos))
            return i;
    }
    return -1;
}

void GalleryGrid::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = itemAt(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void GalleryGrid::mouseReleaseEvent(QMouseEvent *event)
{
    const int item = itemAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && item >= 0)
        Q_EMIT activated(m_items.at(item).picture);
}

void GalleryGrid::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- Filmstrip

Filmstrip::Filmstrip(Thumbnails *thumbnails, QWidget *parent)
    : QWidget(parent)
    , m_thumbnails(thumbnails)
{
    setMouseTracking(true);
    setFixedHeight(StripTile + 20);
    connect(thumbnails, &Thumbnails::ready, this, qOverload<>(&QWidget::update));
}

void Filmstrip::setPictures(const QStringList &paths, int current)
{
    m_paths = paths;
    m_current = current;
    scrollToCurrent();
    update();
}

void Filmstrip::setCurrent(int index)
{
    m_current = index;
    scrollToCurrent();
    update();
}

QRect Filmstrip::tileRect(int index) const
{
    return QRect(index * (StripTile + StripGap) - m_offset, (height() - StripTile) / 2, StripTile, StripTile);
}

void Filmstrip::scrollToCurrent()
{
    // The current picture in the middle when the strip is longer than the view.
    const int total = int(m_paths.size()) * (StripTile + StripGap) - StripGap;
    if (total <= width()) {
        m_offset = -(width() - total) / 2;
        return;
    }
    const int center = m_current * (StripTile + StripGap) + StripTile / 2;
    m_offset = qBound(0, center - width() / 2, total - width());
}

int Filmstrip::tileAt(const QPoint &pos) const
{
    for (int i = 0; i < m_paths.size(); ++i) {
        if (tileRect(i).contains(pos))
            return i;
    }
    return -1;
}

void Filmstrip::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < m_paths.size(); ++i) {
        const QRect r = tileRect(i);
        if (r.right() < 0 || r.left() > width())
            continue;
        drawThumbnail(painter, r, m_thumbnails->get(m_paths.at(i)), 10);
        if (i == m_current || i == m_hover) {
            painter.setPen(QPen(i == m_current ? t.accent : t.muted, i == m_current ? 3 : 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5), 9, 9);
        }
    }
}

void Filmstrip::mouseMoveEvent(QMouseEvent *event)
{
    const int hover = tileAt(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void Filmstrip::mouseReleaseEvent(QMouseEvent *event)
{
    const int tile = tileAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && tile >= 0)
        Q_EMIT activated(tile);
}

void Filmstrip::wheelEvent(QWheelEvent *event)
{
    const int total = int(m_paths.size()) * (StripTile + StripGap) - StripGap;
    if (total <= width())
        return;
    m_offset = qBound(0, m_offset - event->angleDelta().y() / 2 - event->angleDelta().x() / 2, total - width());
    update();
}

void Filmstrip::resizeEvent(QResizeEvent *) { scrollToCurrent(); }

void Filmstrip::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}
