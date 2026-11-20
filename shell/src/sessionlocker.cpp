// SPDX-License-Identifier: GPL-3.0-or-later
#include "sessionlocker.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusInterface>
#include <QProcess>
#include <QSettings>

#include <unistd.h>

namespace {

const QString Login1 = QStringLiteral("org.freedesktop.login1");

} // namespace

SessionLocker::SessionLocker(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusInterface manager(Login1, QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
                           bus);
    const QDBusReply<QDBusObjectPath> session = manager.call(QStringLiteral("GetSessionByPID"), uint(getpid()));
    if (session.isValid()) {
        bus.connect(Login1, session.value().path(), QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Lock"),
                    this, SLOT(lock()));
        bus.connect(Login1, session.value().path(), QStringLiteral("org.freedesktop.DBus.Properties"),
                    QStringLiteral("PropertiesChanged"), this,
                    SLOT(onSessionChanged(QString, QVariantMap, QStringList)));
    }
    bus.connect(Login1, QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
                QStringLiteral("PrepareForSleep"), this, SLOT(onPrepareForSleep(bool)));
}

void SessionLocker::lock()
{
    // A second lock screen exits at once: the compositor refuses it.
    QProcess::startDetached(QStringLiteral("karyo-lock"), {});
}

void SessionLocker::onPrepareForSleep(bool start)
{
    if (start)
        lock();
}

void SessionLocker::onSessionChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != QLatin1String("org.freedesktop.login1.Session") || !changed.contains(QStringLiteral("Active"))
        || changed.value(QStringLiteral("Active")).toBool())
        return;
    const QSettings shell(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                          QStringLiteral("shell"));
    if (!qEnvironmentVariableIsEmpty("OPENKARYON_DESKTOP_PASSWORD")
        || shell.value(QStringLiteral("Desktops/lockOnLeave"), false).toBool())
        lock();
}
