// SPDX-License-Identifier: GPL-3.0-or-later
#include "sessionlocker.h"
#include "desktopsettings.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusInterface>
#include <QProcess>
#include <QSettings>
#include <QPointer>
#include <cstdio>

#include <unistd.h>

namespace {

QPointer<SessionLocker> locker;
const QString Login1 = QStringLiteral("org.freedesktop.login1");

} // namespace

SessionLocker::SessionLocker(QObject *parent)
    : QObject(parent)
{
    locker = this;
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_output += m_process.readAllStandardOutput();
        if (m_output.contains("KARYO_LOCKED\n")) {
            m_ready = true;
            if (m_preparingForSleep)
                m_sleepDelay = {};
        }
        if (m_output.size() > 4096)
            m_output = m_output.right(4096);
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        std::fputs(m_process.readAllStandardError().constData(), stderr);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            std::fputs("karyo-shell: could not start the session lock\n", stderr);
    });
    acquireSleepDelay();
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

void SessionLocker::acquireSleepDelay()
{
    QDBusInterface manager(Login1, QStringLiteral("/org/freedesktop/login1"),
                           QStringLiteral("org.freedesktop.login1.Manager"), QDBusConnection::systemBus());
    const QDBusReply<QDBusUnixFileDescriptor> reply = manager.call(
        QStringLiteral("Inhibit"), QStringLiteral("sleep"), QStringLiteral("Karyo"),
        QStringLiteral("Lock the session before sleep"), QStringLiteral("delay"));
    if (reply.isValid())
        m_sleepDelay = reply.value();
}

void SessionLocker::startLock()
{
    if (m_process.state() != QProcess::NotRunning)
        return;
    m_output.clear();
    m_ready = false;
    m_process.start(QStringLiteral("karyo-lock"));
}

void SessionLocker::lock()
{
    if (locker)
        locker->startLock();
    else
        QProcess::startDetached(QStringLiteral("karyo-lock"), {});
}

void SessionLocker::onPrepareForSleep(bool start)
{
    m_preparingForSleep = start;
    if (start) {
        if (m_process.state() != QProcess::NotRunning && m_ready)
            m_sleepDelay = {};
        else
            lock();
    } else {
        acquireSleepDelay();
    }
}

void SessionLocker::onSessionChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != QLatin1String("org.freedesktop.login1.Session") || !changed.contains(QStringLiteral("Active"))
        || changed.value(QStringLiteral("Active")).toBool())
        return;
    const QSettings shell(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                          QStringLiteral("shell"));
    if (Karyo::Accounts::DesktopSettings::current().protectedEntry()
        || shell.value(QStringLiteral("Desktops/lockOnLeave"), false).toBool())
        lock();
}
