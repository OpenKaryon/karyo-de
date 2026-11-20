// SPDX-License-Identifier: GPL-3.0-or-later
#include "keypad.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QPainter>

namespace Ui = Karyo::Ui;

CalcKey::CalcKey(const QString &label, Kind kind, QWidget *parent)
    : QAbstractButton(parent)
    , m_kind(kind)
{
    setText(label);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::NoFocus);  // typing stays in the display
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoRepeat(false);
}

void CalcKey::setGlyph(const QString &glyph)
{
    m_glyph = glyph;
    update();
}

QSize CalcKey::sizeHint() const { return {76, 54}; }

QSize CalcKey::minimumSizeHint() const { return {46, 38}; }

void CalcKey::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool hover = isEnabled() && underMouse();
    const bool down = isEnabled() && isDown();
    QColor fill;
    QColor ink = t.text;
    switch (m_kind) {
    case Kind::Digit:
        fill = down ? Ui::mix(t.text, t.card, 13) : hover ? Ui::mix(t.text, t.card, 6) : t.card;
        break;
    case Kind::Function:
        fill = down ? Ui::mix(t.text, t.pane, 13) : hover ? Ui::mix(t.text, t.pane, 6) : t.pane;
        break;
    case Kind::Operator:
        fill = down ? Ui::alpha(t.accent, 30) : hover ? Ui::alpha(t.accent, 22) : t.tint1;
        ink = t.ink1;
        break;
    case Kind::Equals:
        fill = down ? Ui::accent(700) : hover ? Ui::accent(600) : t.accent;
        ink = Ui::neutral(100);
        break;
    }
    if (!isEnabled())
        ink = Ui::alpha(ink, 45);
    Ui::fillRounded(painter, QRectF(rect()).adjusted(2, 2, -2, -2), 14, fill);

    if (!m_glyph.isEmpty()) {
        const int size = 20;
        painter.drawPixmap((width() - size) / 2, (height() - size) / 2,
                           Ui::icon(m_glyph, size, ink, devicePixelRatioF()));
        return;
    }
    // Operator signs are thin; they get a larger size than the digits.
    const bool digit = m_kind == Kind::Digit || m_kind == Kind::Equals;
    const qreal size = m_kind == Kind::Operator || m_kind == Kind::Equals ? 26 : digit ? 21 : 16.5;
    painter.setFont(Ui::bodyFont(size, digit ? QFont::DemiBold : QFont::Normal));
    painter.setPen(ink);
    painter.drawText(rect(), Qt::AlignCenter, text());
}
