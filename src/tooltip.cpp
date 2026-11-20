// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/tooltip.h"

#include "karyo/ui/paint.h"
#include "karyo/ui/theme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPointer>

namespace Karyo::Ui {

namespace {

constexpr int PadX = 14;
constexpr int PadY = 8;
constexpr int MaxTextWidth = 260 - 2 * PadX;
constexpr qreal TitleSize = 12.5;

QPointer<ToolTip> &shared()
{
    static QPointer<ToolTip> tip;
    return tip;
}

} // namespace

ToolTip::ToolTip(QWidget *parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void ToolTip::showAbove(QWidget *anchor, const QString &title, const QString &subtitle, int gap)
{
    if (!anchor || title.isEmpty())
        return;
    QPointer<ToolTip> &tip = shared();
    // A tooltip is a popup of the anchor's window, so it is recreated per window.
    if (tip && tip->parentWidget() != anchor->window()) {
        tip->deleteLater();
        tip.clear();
    }
    if (!tip)
        tip = new ToolTip(anchor->window());
    tip->m_anchor = anchor;
    tip->setText(title, subtitle);
    const QPoint anchorTop = anchor->mapToGlobal(QPoint(anchor->width() / 2, 0));
    tip->move(anchorTop.x() - tip->width() / 2, anchorTop.y() - gap - tip->height());
    tip->show();
    tip->raise();
}

void ToolTip::hideFor(QWidget *anchor)
{
    QPointer<ToolTip> &tip = shared();
    if (tip && tip->m_anchor == anchor)
        tip->hide();
}

void ToolTip::setText(const QString &title, const QString &subtitle)
{
    m_title = title;
    m_subtitle = subtitle;
    const QFontMetrics titleMetrics(bodyFont(TitleSize, QFont::DemiBold));
    const QFontMetrics subMetrics(bodyFont(TitleSize));
    const int lineHeight = qRound(TitleSize * 1.4);
    int textWidth = titleMetrics.horizontalAdvance(title);
    if (!subtitle.isEmpty())
        textWidth = qMax(textWidth, subMetrics.horizontalAdvance(subtitle));
    textWidth = qMin(textWidth, MaxTextWidth);
    const int lines = subtitle.isEmpty() ? 1 : 2;
    // One spare pixel keeps fractional advances from eliding the text.
    setFixedSize(textWidth + 1 + 2 * PadX, lines * lineHeight + 2 * PadY);
    update();
}

void ToolTip::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    fillRounded(painter, rect(), 8, neutral(900));
    const int lineHeight = qRound(TitleSize * 1.4);
    QRect line(PadX, PadY, width() - 2 * PadX, lineHeight);
    painter.setFont(bodyFont(TitleSize, QFont::DemiBold));
    painter.setPen(neutral(100));
    painter.drawText(line, Qt::AlignLeft | Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(m_title, Qt::ElideRight, line.width()));
    if (!m_subtitle.isEmpty()) {
        line.translate(0, lineHeight);
        painter.setFont(bodyFont(TitleSize));
        painter.setPen(neutral(400));
        painter.drawText(line, Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(m_subtitle, Qt::ElideRight, line.width()));
    }
}

} // namespace Karyo::Ui
