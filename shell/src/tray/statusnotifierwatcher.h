// SPDX-License-Identifier: GPL-3.0-or-later
// org.kde.StatusNotifierWatcher, served by the shell when nobody else does.
#pragma once

#include <QDBusContext>
#include <QObject>
#include <QStringList>

class QDBusServiceWatcher;

class StatusNotifierWatcher : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ isHostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)

public:
    explicit StatusNotifierWatcher(QObject *parent = nullptr);
    ~StatusNotifierWatcher() override;

    // Exports the object and claims the well-known name; false if it is taken.
    bool start();

    QStringList registeredItems() const { return m_items; }
    bool isHostRegistered() const { return !m_hosts.isEmpty(); }
    int protocolVersion() const { return 0; }

public Q_SLOTS:
    void RegisterStatusNotifierItem(const QString &serviceOrPath);
    void RegisterStatusNotifierHost(const QString &service);

Q_SIGNALS:
    void StatusNotifierItemRegistered(const QString &item);
    void StatusNotifierItemUnregistered(const QString &item);
    void StatusNotifierHostRegistered();

private:
    void serviceUnregistered(const QString &service);

    QStringList m_items;
    QStringList m_hosts;
    QDBusServiceWatcher *m_serviceWatcher;
};
