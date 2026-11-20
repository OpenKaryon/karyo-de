// SPDX-License-Identifier: GPL-3.0-or-later
#include "flyout.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>

namespace Ui = Karyo::Ui;

Flyout::Flyout(QScreen *screen)
    : QWidget(nullptr, Qt::FramelessWindowHint)
    , m_screen(screen)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    qApp->installEventFilter(this);
}

void Flyout::setToggleWidgets(const QList<QWidget *> &widgets)
{
    m_toggleWidgets.clear();
    for (QWidget *widget : widgets)
        m_toggleWidgets.append(widget);
}

void Flyout::place(LayerSurface::Anchors anchors, const QMargins &margins)
{
    LayerSurface::configure(this, m_screen, LayerSurface::LayerTop, anchors, -1);
    LayerSurface::setMargins(this, margins);
    // labwc gives an on-demand surface in the top layer the keyboard when it
    // maps, and takes it back when the user clicks a window: the flyout closes.
    LayerSurface::setKeyboard(this, LayerSurface::KeyboardOnDemand);
}

void Flyout::paintCard(QPainter &painter, const QRectF &card, qreal radius)
{
    const Ui::Theme &t = Ui::Theme::current();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    Ui::drawLargeShadow(painter, card, radius);
    // --k-glass-strong as it reads over the desktop: labwc has no backdrop
    // blur, and windows showing through the glass would muddy the card.
    QColor glass = t.glassStrong;
    const qreal coverage = glass.alphaF() * 100;
    glass.setAlpha(255);
    Ui::fillRounded(painter, card, radius, Ui::mix(glass, t.desk, coverage), t.edge);
    painter.restore();
}

void Flyout::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool Flyout::event(QEvent *event)
{
    if (event->type() == QEvent::WindowActivate) {
        m_wasActive = true;
    } else if (event->type() == QEvent::WindowDeactivate && m_wasActive
               && !QApplication::activePopupWidget()) {
        // Focus went to a window.
        close();
    }
    return QWidget::event(event);
}

bool Flyout::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        // A click in another surface of the shell (taskbar, wallpaper, another
        // flyout) closes this one, except on the buttons that toggle it.
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->window() != this && !qobject_cast<QMenu *>(widget->window())) {
            for (const auto &toggle : std::as_const(m_toggleWidgets)) {
                if (toggle && (toggle == widget || toggle->isAncestorOf(widget)))
                    return false;
            }
            close();
        }
    }
    return QWidget::eventFilter(watched, event);
}
