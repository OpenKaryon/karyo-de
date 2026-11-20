// SPDX-License-Identifier: GPL-3.0-or-later
// Notification center (KaryonPanels.dc.html, variant notify): a column at the
// right edge with the notifications and "Clear all" on top, the month
// calendar and a Focus session below. It opens from the clock.
#pragma once

#include "flyout.h"

class NotificationServer;
class QLabel;
class QVBoxLayout;

class NotificationCenter : public Flyout
{
    Q_OBJECT
public:
    NotificationCenter(NotificationServer *server, QScreen *screen);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void rebuild();

    NotificationServer *m_server;
    QWidget *m_listCard;
    QWidget *m_calendarCard;
    QVBoxLayout *m_list;
    QLabel *m_empty;
};
