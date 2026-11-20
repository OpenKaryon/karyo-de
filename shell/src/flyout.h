// SPDX-License-Identifier: GPL-3.0-or-later
// What Start, Quick settings and the Notification center share: an on-demand
// layer surface that holds the keyboard while it is open and closes on
// Escape, on a click in another surface of the shell, and when a window
// takes the keyboard. It deletes itself when closed.
#pragma once

#include "layersurface.h"

#include <QList>
#include <QPointer>
#include <QWidget>

class QPainter;
class QScreen;

class Flyout : public QWidget
{
    Q_OBJECT
public:
    explicit Flyout(QScreen *screen);

    // Clicks on these widgets (the taskbar button that opened the flyout)
    // toggle it themselves instead of closing it as clicks outside.
    void setToggleWidgets(const QList<QWidget *> &widgets);

    // The opaque glass card with its shadow, as Start and the flyouts draw it.
    static void paintCard(QPainter &painter, const QRectF &card, qreal radius);

protected:
    // Anchors the surface and keeps it margins away from those screen edges.
    // The taskbar's height is not reserved: margins count from the edge.
    void place(LayerSurface::Anchors anchors, const QMargins &margins);

    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    QScreen *m_screen;

private:
    QList<QPointer<QWidget>> m_toggleWidgets;
    bool m_wasActive = false;
};
