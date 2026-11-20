// SPDX-License-Identifier: GPL-3.0-or-later
#include "statusnotifierwatcher.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>

namespace {
const QString WatcherService = QStringLiteral("org.kde.StatusNotifierWatcher");
const QString WatcherPath = QStringLiteral("/StatusNotifierWatcher");
} // namespace

StatusNotifierWatcher::StatusNotifierWatcher(QObject *parent)
    : QObject(parent)
    , m_serviceWatcher(new QDBusServiceWatcher(this))
{
    m_serviceWatcher->setConnection(QDBusConnection::sessionBus());
    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &StatusNotifierWatcher::serviceUnregistered);
}

StatusNotifierWatcher::~StatusNotifierWatcher()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterService(WatcherService);
    bus.unregisterObject(WatcherPath);
}

bool StatusNotifierWatcher::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerObject(WatcherPath, this,
                            QDBusConnection::ExportScriptableContents
                                | QDBusConnection::ExportAllProperties
                                | QDBusConnection::ExportAllSlots
                                | QDBusConnection::ExportAllSignals))
        return false;
    const auto reply = bus.interface()->registerService(
        WatcherService, QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::DontAllowReplacement);
    if (!reply.isValid() || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
        bus.unregisterObject(WatcherPath);
        return false;
    }
    return true;
}

void StatusNotifierWatcher::RegisterStatusNotifierItem(const QString &serviceOrPath)
{
    // Items register either their bus name or, like libappindicator, an object path.
    QString service = serviceOrPath;
    QString path = QStringLiteral("/StatusNotifierItem");
    if (serviceOrPath.startsWith(QLatin1Char('/'))) {
        service = calledFromDBus() ? message().service() : QString();
        path = serviceOrPath;
    }
    if (service.isEmpty())
        return;
    const QString item = service + path;
    if (m_items.contains(item))
        return;
    m_items.append(item);
    m_serviceWatcher->addWatchedService(service);
    Q_EMIT StatusNotifierItemRegistered(item);
}

void StatusNotifierWatcher::RegisterStatusNotifierHost(const QString &service)
{
    if (m_hosts.contains(service))
        return;
    m_hosts.append(service);
    m_serviceWatcher->addWatchedService(service);
    Q_EMIT StatusNotifierHostRegistered();
}

void StatusNotifierWatcher::serviceUnregistered(const QString &service)
{
    m_hosts.removeAll(service);
    const QString prefix = service + QLatin1Char('/');
    for (qsizetype i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).startsWith(prefix)) {
            const QString item = m_items.takeAt(i);
            Q_EMIT StatusNotifierItemUnregistered(item);
        }
    }
    m_serviceWatcher->removeWatchedService(service);
}
