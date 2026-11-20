// SPDX-License-Identifier: GPL-3.0-or-later
#include "notificationserver.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QImage>
#include <QPixmap>
#include <QTextDocumentFragment>
#include <QUrl>

namespace {

constexpr int MaxKept = 50;

// The image-data hint: (iiibiiay) width, height, rowstride, has alpha, bits
// per sample, channels, pixels.
QImage imageFromHint(const QVariant &value)
{
    if (!value.canConvert<QDBusArgument>())
        return {};
    const QDBusArgument arg = value.value<QDBusArgument>();
    int width = 0, height = 0, rowstride = 0, bitsPerSample = 0, channels = 0;
    bool hasAlpha = false;
    QByteArray pixels;
    arg.beginStructure();
    arg >> width >> height >> rowstride >> hasAlpha >> bitsPerSample >> channels >> pixels;
    arg.endStructure();
    if (width <= 0 || height <= 0 || bitsPerSample != 8 || pixels.size() < rowstride * height)
        return {};
    const QImage::Format format = hasAlpha && channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    if (!hasAlpha && channels != 3)
        return {};
    return QImage(reinterpret_cast<const uchar *>(pixels.constData()), width, height, rowstride, format).copy();
}

QIcon iconFromName(const QString &name)
{
    if (name.isEmpty())
        return {};
    if (name.startsWith(QLatin1String("file://")))
        return QIcon(QUrl(name).toLocalFile());
    if (name.startsWith(QLatin1Char('/')))
        return QIcon(name);
    return QIcon::fromTheme(name);
}

} // namespace

NotificationServer::NotificationServer(QObject *parent)
    : QObject(parent)
{
    m_focusTimer.setSingleShot(true);
    connect(&m_focusTimer, &QTimer::timeout, this, [this] { setDoNotDisturb(false); });
}

bool NotificationServer::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), this,
                            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals))
        return false;
    return bus.registerService(QStringLiteral("org.freedesktop.Notifications"));
}

uint NotificationServer::Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                                const QString &summary, const QString &body, const QStringList &actions,
                                const QVariantMap &hints, int expire_timeout)
{
    Notification n;
    n.appName = app_name;
    n.appIcon = app_icon;
    n.summary = summary;
    n.body = QTextDocumentFragment::fromHtml(body).toPlainText();
    n.actions = actions;
    n.time = QDateTime::currentDateTime();
    n.timeout = expire_timeout;
    n.desktopEntry = hints.value(QStringLiteral("desktop-entry")).toString();
    n.critical = hints.value(QStringLiteral("urgency")).toUInt() == 2;
    n.resident = hints.value(QStringLiteral("resident")).toBool();
    for (const QString &key : {QStringLiteral("image-data"), QStringLiteral("image_data"), QStringLiteral("icon_data")}) {
        const QImage image = imageFromHint(hints.value(key));
        if (!image.isNull()) {
            n.image = QIcon(QPixmap::fromImage(image));
            break;
        }
    }
    if (n.image.isNull())
        n.image = iconFromName(hints.value(QStringLiteral("image-path")).toString());
    if (n.image.isNull())
        n.image = iconFromName(app_icon);

    bool replaced = false;
    if (replaces_id != 0) {
        for (Notification &existing : m_notifications) {
            if (existing.id == replaces_id) {
                n.id = replaces_id;
                existing = n;
                replaced = true;
                break;
            }
        }
    }
    if (!replaced) {
        n.id = m_nextId++;
        // Transient notifications show as a banner only.
        if (!hints.value(QStringLiteral("transient")).toBool()) {
            m_notifications.prepend(n);
            while (m_notifications.size() > MaxKept)
                m_notifications.removeLast();
        }
    }
    Q_EMIT changed();
    if (!m_dndOn || n.critical)
        Q_EMIT bannerRequested(n);
    return n.id;
}

void NotificationServer::CloseNotification(uint id) { remove(id, Closed); }

QStringList NotificationServer::GetCapabilities()
{
    return {QStringLiteral("actions"), QStringLiteral("body"), QStringLiteral("icon-static"),
            QStringLiteral("persistence")};
}

QString NotificationServer::GetServerInformation(QString &vendor, QString &version, QString &spec_version)
{
    vendor = QStringLiteral("OpenKaryon");
    version = QStringLiteral("0.2");
    spec_version = QStringLiteral("1.2");
    return QStringLiteral("karyo-shell");
}

void NotificationServer::dismiss(uint id) { remove(id, Dismissed); }

void NotificationServer::dismissAll()
{
    const QList<Notification> all = m_notifications;
    m_notifications.clear();
    for (const Notification &n : all)
        Q_EMIT NotificationClosed(n.id, Dismissed);
    Q_EMIT changed();
}

void NotificationServer::invoke(uint id, const QString &actionKey)
{
    Q_EMIT ActionInvoked(id, actionKey);
    for (const Notification &n : std::as_const(m_notifications)) {
        if (n.id == id) {
            if (!n.resident)
                remove(id, Dismissed);
            return;
        }
    }
}

void NotificationServer::setDoNotDisturb(bool on)
{
    if (!on) {
        m_focusTimer.stop();
        m_focusUntil = QDateTime();
    }
    if (m_dndOn == on)
        return;
    m_dndOn = on;
    Q_EMIT doNotDisturbChanged(on);
}

void NotificationServer::startFocus(int minutes)
{
    m_focusUntil = QDateTime::currentDateTime().addSecs(qint64(minutes) * 60);
    m_focusTimer.start(minutes * 60 * 1000);
    if (m_dndOn)
        Q_EMIT doNotDisturbChanged(true);  // the countdown changed
    else
        setDoNotDisturb(true);
}

void NotificationServer::remove(uint id, CloseReason reason)
{
    for (qsizetype i = 0; i < m_notifications.size(); ++i) {
        if (m_notifications.at(i).id == id) {
            m_notifications.removeAt(i);
            Q_EMIT NotificationClosed(id, reason);
            Q_EMIT changed();
            return;
        }
    }
    // Banner-only (transient) notifications still report their closing.
    Q_EMIT NotificationClosed(id, reason);
}
