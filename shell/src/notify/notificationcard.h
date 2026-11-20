// SPDX-License-Identifier: GPL-3.0-or-later
// One notification as KaryonPanels.dc.html draws it: app tile, app name and
// time, the title, the body and the action buttons, on a --k-card card.
// The Notification center lists them; a banner shows the newest one.
#pragma once

#include "notificationserver.h"

#include <QList>
#include <QWidget>

class QAbstractButton;

class NotificationCard : public QWidget
{
    Q_OBJECT
public:
    NotificationCard(const Notification &notification, NotificationServer *server, QWidget *parent);

    uint id() const { return m_notification.id; }
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }
    QSize sizeHint() const override;

Q_SIGNALS:
    // The close button was used; the owner decides what closing means
    // (the center forgets the notification, a banner just goes away).
    void dismissed();
    // The card or one of its actions was used.
    void used();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect closeRect() const;
    int bodyHeight(int width) const;
    void placeButtons();

    Notification m_notification;
    NotificationServer *m_server;
    QString m_appName;
    QList<QAbstractButton *> m_buttons;
    bool m_hover = false;
    bool m_closeHover = false;
};
