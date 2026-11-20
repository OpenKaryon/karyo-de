// SPDX-License-Identifier: GPL-3.0-or-later
// A new notification shown for a few seconds above the tray, as a card like
// the Notification center's. It stays while the pointer is on it; critical
// notifications stay until closed.
#pragma once

#include "notificationserver.h"

#include <QTimer>
#include <QWidget>

class NotificationCard;
class QScreen;

class NotificationBanner : public QWidget
{
    Q_OBJECT
public:
    NotificationBanner(NotificationServer *server, QScreen *screen);
    void showNotification(const Notification &notification);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect cardRect() const;

    NotificationServer *m_server;
    NotificationCard *m_card = nullptr;
    QTimer m_timer;
    bool m_sticky = false;
};
