// SPDX-License-Identifier: GPL-3.0-or-later
#include "thumbnails.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>

namespace Ui = Karyo::Ui;

namespace {

constexpr int ThumbWidth = 132;
constexpr int Gap = 30;  // room for the page number under each page
constexpr int MaxCached = 60;

} // namespace

Thumbnails::Thumbnails(QPdfDocument *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
{
    setCursor(Qt::PointingHandCursor);
    connect(document, &QPdfDocument::statusChanged, this, &Thumbnails::reset);
}

int Thumbnails::pageHeight(int page) const
{
    const QSizeF points = m_document->pagePointSize(page);
    if (points.isEmpty())
        return ThumbWidth * 4 / 3;
    return qRound(ThumbWidth * points.height() / points.width());
}

void Thumbnails::reset()
{
    m_cache.clear();
    m_tops.clear();
    int y = 12;
    for (int page = 0; page < m_document->pageCount(); ++page) {
        m_tops.append(y);
        y += pageHeight(page) + Gap;
    }
    setFixedHeight(y);
    update();
}

QSize Thumbnails::sizeHint() const { return {ThumbWidth + 48, height()}; }

int Thumbnails::pageTop(int page) const { return m_tops.value(page); }

QRect Thumbnails::pageRect(int page) const
{
    return QRect((width() - ThumbWidth) / 2, m_tops.value(page), ThumbWidth, pageHeight(page));
}

void Thumbnails::setCurrent(int page)
{
    if (page == m_current)
        return;
    m_current = page;
    update();
}

void Thumbnails::paintEvent(QPaintEvent *event)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal dpr = devicePixelRatioF();
    for (int page = 0; page < m_tops.size(); ++page) {
        const QRect r = pageRect(page);
        if (r.bottom() + Gap < event->rect().top() || r.top() > event->rect().bottom())
            continue;
        // Rendered when first seen; far ones are dropped to bound memory.
        if (!m_cache.contains(page)) {
            if (m_cache.size() >= MaxCached)
                m_cache.erase(m_cache.begin());
            m_cache.insert(page, m_document->render(page, r.size() * dpr));
        }
        const QImage &image = m_cache.value(page);
        p.fillRect(r, Qt::white);
        p.drawImage(r, image);
        if (page == m_current) {
            p.setPen(QPen(t.accent, 3));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(r).adjusted(-3, -3, 3, 3), 4, 4);
        } else {
            p.setPen(QPen(t.edge, 1));
            p.drawRect(QRectF(r).adjusted(-0.5, -0.5, 0.5, 0.5));
        }
        p.setFont(Ui::bodyFont(12, page == m_current ? QFont::DemiBold : QFont::Normal));
        p.setPen(page == m_current ? t.text : t.muted);
        p.drawText(QRect(0, r.bottom() + 6, width(), 18), Qt::AlignCenter, QString::number(page + 1));
    }
}

void Thumbnails::mouseReleaseEvent(QMouseEvent *event)
{
    for (int page = 0; page < m_tops.size(); ++page) {
        if (pageRect(page).adjusted(-4, -4, 4, Gap).contains(event->position().toPoint())) {
            Q_EMIT activated(page);
            return;
        }
    }
}
