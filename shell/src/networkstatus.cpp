// SPDX-License-Identifier: GPL-3.0-or-later
#include "networkstatus.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace {
const QString NmService = QStringLiteral("org.freedesktop.NetworkManager");
const QString NmPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
// NM_STATE_CONNECTED_SITE and NM_STATE_CONNECTED_GLOBAL.
constexpr uint ConnectedSite = 60;
// NM_DEVICE_TYPE_WIFI.
constexpr uint DeviceWifi = 2;

QDBusPendingCall getProperty(const QString &path, const QString &interface, const QString &name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(NmService, path, Properties, QStringLiteral("Get"));
    call << interface << name;
    return QDBusConnection::systemBus().asyncCall(call);
}
} // namespace

NetworkStatus::NetworkStatus(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(NmService, NmPath, Properties, QStringLiteral("PropertiesChanged"), this, SLOT(refresh()));
    bus.connect(NmService, NmPath, NmService, QStringLiteral("StateChanged"), this, SLOT(refresh()));
    bus.connect(NmService, NmPath, NmService, QStringLiteral("DeviceAdded"), this, SLOT(refreshDevices()));
    bus.connect(NmService, NmPath, NmService, QStringLiteral("DeviceRemoved"), this, SLOT(refreshDevices()));
    refresh();
    refreshDevices();
}

QString NetworkStatus::glyph() const
{
    if (m_connected && isWired())
        return QStringLiteral("network");
    return QStringLiteral("wifi");
}

void NetworkStatus::setWirelessEnabled(bool enabled)
{
    QDBusMessage call = QDBusMessage::createMethodCall(NmService, NmPath, Properties, QStringLiteral("Set"));
    call << NmService << QStringLiteral("WirelessEnabled") << QVariant::fromValue(QDBusVariant(enabled));
    QDBusConnection::systemBus().asyncCall(call);
}

void NetworkStatus::refresh()
{
    QDBusMessage call = QDBusMessage::createMethodCall(NmService, NmPath, Properties, QStringLiteral("GetAll"));
    call << NmService;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        const QVariantMap props = reply.isError() ? QVariantMap() : reply.value();
        m_type = props.value(QStringLiteral("PrimaryConnectionType")).toString();
        m_connected = props.value(QStringLiteral("State")).toUInt() >= ConnectedSite;
        m_wirelessEnabled = props.value(QStringLiteral("WirelessEnabled")).toBool();
        const QString primary = qvariant_cast<QDBusObjectPath>(props.value(QStringLiteral("PrimaryConnection"))).path();
        if (primary.isEmpty() || primary == QLatin1String("/")) {
            m_name.clear();
            Q_EMIT changed();
            return;
        }
        auto *name = new QDBusPendingCallWatcher(
            getProperty(primary, QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"),
                        QStringLiteral("Id")),
            this);
        connect(name, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *n) {
            n->deleteLater();
            QDBusPendingReply<QDBusVariant> id = *n;
            m_name = id.isError() ? QString() : id.value().variant().toString();
            Q_EMIT changed();
        });
    });
}

void NetworkStatus::refreshDevices()
{
    QDBusMessage call = QDBusMessage::createMethodCall(NmService, NmPath, NmService, QStringLiteral("GetDevices"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QList<QDBusObjectPath>> reply = *w;
        if (reply.isError())
            return;
        m_hasWireless = false;
        for (const QDBusObjectPath &device : reply.value()) {
            auto *type = new QDBusPendingCallWatcher(
                getProperty(device.path(), QStringLiteral("org.freedesktop.NetworkManager.Device"),
                            QStringLiteral("DeviceType")),
                this);
            connect(type, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *t) {
                t->deleteLater();
                QDBusPendingReply<QDBusVariant> value = *t;
                if (!value.isError() && value.value().variant().toUInt() == DeviceWifi && !m_hasWireless) {
                    m_hasWireless = true;
                    Q_EMIT changed();
                }
            });
        }
    });
}
