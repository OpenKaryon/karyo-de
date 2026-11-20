// SPDX-License-Identifier: GPL-3.0-or-later
// org.karyo.Shell on the session bus: what labwc key bindings ask of the
// running shell ("karyo-shell --start" calls ToggleStart).
#pragma once

#include <QObject>

class NotificationServer;
class Taskbar;

class ShellService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.karyo.Shell")
public:
    static constexpr const char *ServiceName = "org.karyo.Shell";
    static constexpr const char *ObjectPath = "/org/karyo/Shell";

    ShellService(Taskbar *taskbar, NotificationServer *notifications, QObject *parent);
    // Claims the bus name; false if another shell holds it.
    bool registerOnBus();

    // Client side: asks the running shell to call method. Returns false
    // when no shell answers.
    static bool call(const QString &method);

public Q_SLOTS:
    Q_SCRIPTABLE void ToggleStart();
    // Settings > System > Notifications.
    Q_SCRIPTABLE bool DoNotDisturb();
    Q_SCRIPTABLE void SetDoNotDisturb(bool on);

private:
    Taskbar *m_taskbar;
    NotificationServer *m_notifications;
};
