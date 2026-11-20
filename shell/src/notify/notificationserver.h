// SPDX-License-Identifier: GPL-3.0-or-later
// org.freedesktop.Notifications in the shell: keeps the notifications for the
// Notification center, asks for banners and owns Do not disturb.
#pragma once

#include <QDateTime>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

struct Notification
{
    uint id = 0;
    QString appName;
    QString desktopEntry;  // hint "desktop-entry", without .desktop
    QString appIcon;
    QString summary;
    QString body;            // plain text; markup is stripped
    QStringList actions;     // key, label, key, label...
    QIcon image;             // hints image-data / image-path, or app_icon
    QDateTime time;
    int timeout = -1;        // ms; -1 server default, 0 never
    bool critical = false;
    bool resident = false;
};

class NotificationServer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationServer(QObject *parent = nullptr);
    // Claims org.freedesktop.Notifications; false if another daemon has it.
    bool registerOnBus();

    const QList<Notification> &notifications() const { return m_notifications; }
    // The user dismissed it, or cleared all.
    void dismiss(uint id);
    void dismissAll();
    // A button or the card itself (key "default") was used.
    void invoke(uint id, const QString &actionKey);

    bool doNotDisturb() const { return m_dndOn; }
    void setDoNotDisturb(bool on);
    // Focus session: Do not disturb until the time passes.
    void startFocus(int minutes);
    QDateTime focusUntil() const { return m_focusUntil; }

public Q_SLOTS:
    Q_SCRIPTABLE uint Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                             const QString &summary, const QString &body, const QStringList &actions,
                             const QVariantMap &hints, int expire_timeout);
    Q_SCRIPTABLE void CloseNotification(uint id);
    Q_SCRIPTABLE QStringList GetCapabilities();
    Q_SCRIPTABLE QString GetServerInformation(QString &vendor, QString &version, QString &spec_version);

Q_SIGNALS:
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);
    Q_SCRIPTABLE void ActionInvoked(uint id, const QString &action_key);

    // For the shell.
    void changed();
    // A new or replaced notification that should show as a banner.
    void bannerRequested(const Notification &notification);
    void doNotDisturbChanged(bool on);

private:
    enum CloseReason { Expired = 1, Dismissed = 2, Closed = 3 };
    void remove(uint id, CloseReason reason);

    QList<Notification> m_notifications;
    uint m_nextId = 1;
    bool m_dndOn = false;
    QDateTime m_focusUntil;
    QTimer m_focusTimer;
};
