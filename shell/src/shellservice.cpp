// SPDX-License-Identifier: GPL-3.0-or-later
#include "shellservice.h"

#include "notify/notificationserver.h"
#include "taskbar.h"

#include <QDBusConnection>
#include <QDBusMessage>

ShellService::ShellService(Taskbar *taskbar, NotificationServer *notifications, QObject *parent)
    : QObject(parent)
    , m_taskbar(taskbar)
    , m_notifications(notifications)
{
}

bool ShellService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.registerObject(QString::fromLatin1(ObjectPath), this, QDBusConnection::ExportScriptableSlots)
           && bus.registerService(QString::fromLatin1(ServiceName));
}

bool ShellService::call(const QString &method)
{
    const QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(ServiceName), QString::fromLatin1(ObjectPath),
        QString::fromLatin1(ServiceName), method);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

void ShellService::setTaskbar(Taskbar *taskbar) { m_taskbar = taskbar; }

void ShellService::ToggleStart()
{
    if (m_taskbar)
        m_taskbar->toggleStart();
}

bool ShellService::DoNotDisturb() { return m_notifications->doNotDisturb(); }

void ShellService::SetDoNotDisturb(bool on) { m_notifications->setDoNotDisturb(on); }
