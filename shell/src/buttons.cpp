// SPDX-License-Identifier: GPL-3.0-or-later
#include "buttons.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/tooltip.h>

#include <QDateTime>
#include <QEvent>
#include <QFontMetrics>
#include <QLocale>
#include <QPainter>
#include <QTimer>

namespace Ui = Karyo::Ui;

ShellButton::ShellButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::ArrowCursor);
}

void ShellButton::setHighlighted(bool highlighted)
{
    if (m_highlighted == highlighted)
        return;
    m_highlighted = highlighted;
    update();
}

void ShellButton::setHint(const QString &title, const QString &subtitle)
{
    m_hintTitle = title;
    m_hintSubtitle = subtitle;
    setAccessibleName(title);
}

void ShellButton::paintBackground(QPainter &painter, qreal radius)
{
    const Ui::Theme &t = Ui::Theme::current();
    QColor fill = Qt::transparent;
    if (isDown())
        fill = t.press;
    else if (m_highlighted || underMouse())
        fill = t.hover;
    Ui::fillRounded(painter, rect(), radius, fill);
    if (hasFocus() && focusPolicy() != Qt::NoFocus) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(t.accent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius - 1, radius - 1);
        painter.restore();
    }
}

bool ShellButton::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::HoverEnter:
        if (!m_hintTitle.isEmpty())
            Ui::ToolTip::showAbove(this, m_hintTitle, m_hintSubtitle);
        break;
    case QEvent::HoverLeave:
    case QEvent::MouseButtonPress:
    case QEvent::Hide:
        Ui::ToolTip::hideFor(this);
        break;
    default:
        break;
    }
    return QAbstractButton::event(event);
}

StartButton::StartButton(QWidget *parent)
    : ShellButton(parent)
{
    setFixedSize(40, 40);
}

void StartButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintBackground(painter, 6);
    Ui::drawKaryoMark(painter, QRectF(7, 7, 26, 26));
}

GlyphButton::GlyphButton(const QString &glyph, int glyphSize, QSize size, qreal radius,
                         QWidget *parent)
    : ShellButton(parent)
    , m_glyph(glyph)
    , m_glyphSize(glyphSize)
    , m_radius(radius)
{
    setFixedSize(size);
}

void GlyphButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintBackground(painter, m_radius);
    const QPixmap glyph = Ui::icon(m_glyph, m_glyphSize, Ui::Theme::current().text,
                                   devicePixelRatioF());
    painter.drawPixmap((width() - m_glyphSize) / 2, (height() - m_glyphSize) / 2, glyph);
}

DockButton::DockButton(const AppStyle &style, QWidget *parent)
    : ShellButton(parent)
    , m_style(style)
{
    setFixedSize(40, 40);
}

void DockButton::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    setHighlighted(state == State::Focused);
    update();
}

void DockButton::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintBackground(painter, 6);

    m_style.paint(painter, QRectF(6, 5, 28, 28), devicePixelRatioF());

    if (m_state != State::Idle) {
        const bool focused = m_state == State::Focused;
        const qreal w = focused ? 18 : 6;
        Ui::fillPill(painter, QRectF((width() - w) / 2.0, height() - 1 - 4, w, 4),
                     focused ? t.accent : t.muted);
    }
}

LanguageButton::LanguageButton(QWidget *parent)
    : ShellButton(parent)
{
    setFixedHeight(40);
    setCode(QStringLiteral("EN"));
}

void LanguageButton::setCode(const QString &code)
{
    m_code = code;
    QFont font = Ui::bodyFont(13, QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 13 * 0.04);
    setFixedWidth(QFontMetrics(font).horizontalAdvance(m_code) + 24);
    update();
}

void LanguageButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    paintBackground(painter, 6);
    QFont font = Ui::bodyFont(13, QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 13 * 0.04);
    painter.setFont(font);
    painter.setPen(Ui::Theme::current().text);
    painter.drawText(rect(), Qt::AlignCenter, m_code);
}

QuickButton::QuickButton(QWidget *parent)
    : ShellButton(parent)
{
    // padding 0 14, two 18px glyphs, gap 12.
    setFixedSize(14 + 18 + 12 + 18 + 14, 40);
    setAccessibleName(tr("Network and quick settings"));
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void QuickButton::setNetworkGlyph(const QString &glyph, bool connected)
{
    m_networkGlyph = glyph;
    m_connected = connected;
    update();
}

void QuickButton::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    paintBackground(painter, 6);
    const qreal dpr = devicePixelRatioF();
    const int y = (height() - 18) / 2;
    painter.drawPixmap(14, y, Ui::icon(m_networkGlyph, 18, m_connected ? t.text : t.muted, dpr));
    painter.drawPixmap(14 + 18 + 12, y, Ui::icon(QStringLiteral("volume-2"), 18, t.text, dpr));
}

ClockButton::ClockButton(QWidget *parent)
    : ShellButton(parent)
{
    setFixedHeight(40);
    tick();
    // Fire on each minute boundary.
    auto *timer = new QTimer(this);
    timer->setTimerType(Qt::VeryCoarseTimer);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        tick();
        timer->start(60000 - QTime::currentTime().second() * 1000 - QTime::currentTime().msec());
    });
    timer->start(60000 - QTime::currentTime().second() * 1000 - QTime::currentTime().msec());
}

void ClockButton::setNotificationCount(int count)
{
    m_count = count;
    updateWidth();
    update();
}

void ClockButton::tick()
{
    const QLocale locale;
    const QDateTime now = QDateTime::currentDateTime();
    m_time = locale.toString(now.time(), QLocale::ShortFormat);
    // The design shows a four-digit year: 10/19/2026, 19.10.2026.
    QString dateFormat = locale.dateFormat(QLocale::ShortFormat);
    if (!dateFormat.contains(QLatin1String("yyyy")))
        dateFormat.replace(QLatin1String("yy"), QLatin1String("yyyy"));
    m_date = locale.toString(now.date(), dateFormat);
    setHint(locale.toString(now.date(), QLocale::LongFormat));
    updateWidth();
    update();
}

void ClockButton::updateWidth()
{
    const QFontMetrics timeMetrics(Ui::bodyFont(13, QFont::DemiBold));
    const QFontMetrics dateMetrics(Ui::bodyFont(12));
    const int text = qMax(timeMetrics.horizontalAdvance(m_time), dateMetrics.horizontalAdvance(m_date));
    // padding 0 8 0 14, gap 10 before the badge.
    int width = 14 + text + 8;
    if (m_count > 0)
        width += 10 + qMax(22, QFontMetrics(Ui::bodyFont(11, QFont::Bold))
                                       .horizontalAdvance(QString::number(m_count)) + 12);
    setFixedWidth(width);
}

void ClockButton::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    paintBackground(painter, 6);

    int right = width() - 8;
    if (m_count > 0) {
        const QString count = m_count > 99 ? QStringLiteral("99+") : QString::number(m_count);
        const QFont font = Ui::bodyFont(11, QFont::Bold);
        const int badgeWidth = qMax(22, QFontMetrics(font).horizontalAdvance(count) + 12);
        const QRectF badge(right - badgeWidth, (height() - 22) / 2.0, badgeWidth, 22);
        Ui::fillPill(painter, badge, t.accent);
        painter.setFont(font);
        painter.setPen(Ui::neutral(100));
        painter.drawText(badge, Qt::AlignCenter, count);
        right = int(badge.left()) - 10;
    }
    // line-height 1.2: 13px time over 12px date.
    const int timeHeight = qRound(13 * 1.2);
    const int dateHeight = qRound(12 * 1.2);
    const int top = (height() - timeHeight - dateHeight) / 2;
    const QRect textRect(14, top, right - 14, timeHeight + dateHeight);
    painter.setFont(Ui::bodyFont(13, QFont::DemiBold));
    painter.setPen(t.text);
    painter.drawText(QRect(textRect.left(), top, textRect.width(), timeHeight),
                     Qt::AlignRight | Qt::AlignVCenter, m_time);
    painter.setFont(Ui::bodyFont(12));
    painter.setPen(t.muted);
    painter.drawText(QRect(textRect.left(), top + timeHeight, textRect.width(), dateHeight),
                     Qt::AlignRight | Qt::AlignVCenter, m_date);
}
