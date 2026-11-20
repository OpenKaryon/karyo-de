// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/navigation.h"

#include "karyo/ui/icons.h"
#include "karyo/ui/paint.h"
#include "karyo/ui/theme.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace Karyo::Ui {

NavList::NavList(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

int NavList::addItem(const QString &glyph, const QString &label)
{
    Row row;
    row.glyph = glyph;
    row.label = label;
    m_rows.append(row);
    updateGeometry();
    update();
    return int(m_rows.size()) - 1;
}

void NavList::clear()
{
    m_rows.clear();
    m_current = -1;
    m_hover = -1;
    updateGeometry();
    update();
}

void NavList::addSeparator()
{
    Row row;
    row.kind = Row::Separator;
    m_rows.append(row);
    updateGeometry();
}

void NavList::addHeading(const QString &text)
{
    Row row;
    row.kind = Row::Heading;
    row.label = text;
    m_rows.append(row);
    updateGeometry();
}

void NavList::setBadge(int index, const QString &badge)
{
    if (index >= 0 && index < m_rows.size()) {
        m_rows[index].badge = badge;
        update();
    }
}

void NavList::setTrailing(int index, const QString &text)
{
    if (index >= 0 && index < m_rows.size()) {
        m_rows[index].trailing = text;
        update();
    }
}

void NavList::setIndent(int index, int level)
{
    if (index >= 0 && index < m_rows.size()) {
        m_rows[index].indent = level;
        update();
    }
}

void NavList::setExpander(int index, bool expandable, bool expanded)
{
    if (index >= 0 && index < m_rows.size()) {
        m_rows[index].expandable = expandable;
        m_rows[index].expanded = expanded;
        update();
    }
}

bool NavList::isItemVisible(int index) const
{
    return index >= 0 && index < m_rows.size() && m_rows.at(index).visible;
}

void NavList::setItemVisible(int index, bool visible)
{
    if (index >= 0 && index < m_rows.size() && m_rows[index].visible != visible) {
        m_rows[index].visible = visible;
        updateGeometry();
        update();
    }
}

void NavList::setCurrentIndex(int index)
{
    if (index == m_current)
        return;
    m_current = index;
    update();
    Q_EMIT currentChanged(index);
}

void NavList::setRowHeight(int height)
{
    m_rowHeight = height;
    updateGeometry();
    update();
}

int NavList::rowHeight(const Row &row) const
{
    switch (row.kind) {
    case Row::Separator:
        return 17;
    case Row::Heading:
        return 30;
    case Row::Item:
        break;
    }
    return m_rowHeight;
}

QList<int> NavList::layoutRows() const
{
    QList<int> ys;
    int y = 0;
    for (const Row &row : m_rows) {
        if (!row.visible) {
            ys.append(-1);
            continue;
        }
        ys.append(y);
        y += rowHeight(row);
    }
    return ys;
}

QSize NavList::sizeHint() const
{
    int h = 0;
    for (const Row &row : m_rows) {
        if (row.visible)
            h += rowHeight(row);
    }
    return {220, h};
}

QSize NavList::minimumSizeHint() const { return {120, sizeHint().height()}; }

int NavList::rowAt(const QPoint &pos) const
{
    const QList<int> ys = layoutRows();
    for (int i = 0; i < m_rows.size(); ++i) {
        if (ys.at(i) < 0 || m_rows.at(i).kind != Row::Item)
            continue;
        if (pos.y() >= ys.at(i) && pos.y() < ys.at(i) + rowHeight(m_rows.at(i)))
            return i;
    }
    return -1;
}

void NavList::paintEvent(QPaintEvent *)
{
    const Theme &t = Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal dpr = devicePixelRatioF();
    const QList<int> ys = layoutRows();
    const bool expanders = std::any_of(m_rows.cbegin(), m_rows.cend(), [](const Row &r) { return r.expandable; });
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        const int y = ys.at(i);
        if (y < 0)
            continue;
        const int h = rowHeight(row);
        if (row.kind == Row::Separator) {
            painter.fillRect(QRectF(16, y + h / 2.0, width() - 32, 1), t.edge);
            continue;
        }
        if (row.kind == Row::Heading) {
            painter.setFont(bodyFont(12, QFont::DemiBold));
            painter.setPen(t.muted);
            painter.drawText(QRect(16, y, width() - 32, h), Qt::AlignLeft | Qt::AlignBottom, row.label);
            continue;
        }
        const QRectF pill(0, y + 2, width(), h - 4);
        const bool selected = i == m_current;
        if (selected)
            fillRounded(painter, pill, pill.height() / 2.0, t.sel);
        else if (i == m_hover)
            fillRounded(painter, pill, pill.height() / 2.0, t.hover);

        int x = 16 + row.indent * 20;
        if (row.expandable) {
            painter.drawPixmap(x - 2, y + (h - 14) / 2,
                               icon(row.expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
                                    14, t.muted, dpr));
        }
        // The expander column exists only in trees (Files' places).
        if (expanders)
            x += 18;
        if (!row.glyph.isEmpty()) {
            painter.drawPixmap(x, y + (h - 16) / 2, icon(row.glyph, 16, selected ? t.accent : t.text, dpr));
            x += 16 + 10;
        }
        int right = width() - 14;
        if (!row.badge.isEmpty()) {
            const QFont font = bodyFont(11, QFont::Bold);
            const int bw = qMax(22, QFontMetrics(font).horizontalAdvance(row.badge) + 12);
            const QRectF badge(right - bw, y + (h - 22) / 2.0, bw, 22);
            fillRounded(painter, badge, 11, t.accent);
            painter.setFont(font);
            painter.setPen(neutral(100));
            painter.drawText(badge, Qt::AlignCenter, row.badge);
            right = int(badge.left()) - 8;
        }
        if (!row.trailing.isEmpty()) {
            const QFont font = bodyFont(12);
            painter.setFont(font);
            painter.setPen(t.muted);
            const int tw = QFontMetrics(font).horizontalAdvance(row.trailing);
            painter.drawText(QRect(right - tw, y, tw, h), Qt::AlignVCenter | Qt::AlignRight, row.trailing);
            right -= tw + 8;
        }
        painter.setFont(bodyFont(m_textSize, selected ? QFont::DemiBold : QFont::Normal));
        painter.setPen(selected ? t.selInk : t.text);
        const QRect text(x, y, right - x, h);
        painter.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(row.label, Qt::ElideRight, text.width()));
    }
}

void NavList::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const int row = rowAt(pos);
    if (row < 0 || event->button() != Qt::LeftButton)
        return;
    const Row &r = m_rows.at(row);
    if (r.expandable && pos.x() < 16 + r.indent * 20 + 18) {
        Q_EMIT expanderToggled(row);
        return;
    }
    setCurrentIndex(row);
    Q_EMIT activated(row);
}

void NavList::mouseMoveEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position().toPoint());
    if (row != m_hover) {
        m_hover = row;
        update();
    }
}

void NavList::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void NavList::contextMenuEvent(QContextMenuEvent *event)
{
    const int row = rowAt(event->pos());
    if (row >= 0)
        Q_EMIT contextMenuRequested(row, event->globalPos());
}

InsetPanel::InsetPanel(QWidget *parent)
    : QWidget(parent)
    , m_fill(Theme::current().win)
{
    connect(ThemeNotifier::instance(), &ThemeNotifier::changed, this, [this] {
        m_fill = Theme::follow(m_fill);
        update();
    });
}

void InsetPanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    fillRounded(painter, rect(), m_radius, m_fill);
}

} // namespace Karyo::Ui
