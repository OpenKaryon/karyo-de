// SPDX-License-Identifier: GPL-3.0-or-later
#include "trayhost.h"

#include "statusnotifierwatcher.h"
#include "trayitem.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusVariant>

namespace {
const QString WatcherService = QStringLiteral("org.kde.StatusNotifierWatcher");
const QString WatcherPath = QStringLiteral("/StatusNotifierWatcher");
} // namespace

TrayHost::TrayHost(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    auto *watcher = new StatusNotifierWatcher(this);
    if (watcher->start()) {
        m_watcher = watcher;
        connect(watcher, &StatusNotifierWatcher::StatusNotifierItemRegistered, this,
                &TrayHost::addItem);
        connect(watcher, &StatusNotifierWatcher::StatusNotifierItemUnregistered, this,
                &TrayHost::removeItem);
    } else {
        // Another watcher already serves this bus: be one of its hosts.
        delete watcher;
        bus.connect(WatcherService, WatcherPath, WatcherService,
                    QStringLiteral("StatusNotifierItemRegistered"), this, SLOT(addItem(QString)));
        bus.connect(WatcherService, WatcherPath, WatcherService,
                    QStringLiteral("StatusNotifierItemUnregistered"), this,
                    SLOT(removeItem(QString)));
        QDBusMessage get = QDBusMessage::createMethodCall(
            WatcherService, WatcherPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get"));
        get << WatcherService << QStringLiteral("RegisteredStatusNotifierItems");
        const QDBusMessage reply = bus.call(get, QDBus::Block, 1500);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QStringList items =
                reply.arguments().first().value<QDBusVariant>().variant().toStringList();
            for (const QString &id : items)
                addItem(id);
        }
    }

    const QString hostService =
        QStringLiteral("org.kde.StatusNotifierHost-%1").arg(QCoreApplication::applicationPid());
    bus.interface()->registerService(hostService);
    QDBusMessage registerHost = QDBusMessage::createMethodCall(
        WatcherService, WatcherPath, WatcherService, QStringLiteral("RegisterStatusNotifierHost"));
    registerHost << hostService;
    if (m_watcher)
        m_watcher->RegisterStatusNotifierHost(hostService);
    else
        bus.asyncCall(registerHost);
}

QList<TrayItem *> TrayHost::visibleItems() const
{
    QList<TrayItem *> visible;
    for (TrayItem *item : m_items) {
        if (!item->isPassive())
            visible.append(item);
    }
    return visible;
}

void TrayHost::addItem(const QString &id)
{
    for (TrayItem *item : std::as_const(m_items)) {
        if (item->id() == id)
            return;
    }
    auto *item = new TrayItem(id, this);
    m_items.append(item);
    connect(item, &TrayItem::changed, this, &TrayHost::itemsChanged);
    Q_EMIT itemsChanged();
}

void TrayHost::removeItem(const QString &id)
{
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i)->id() == id) {
            m_items.takeAt(i)->deleteLater();
            Q_EMIT itemsChanged();
            return;
        }
    }
}
