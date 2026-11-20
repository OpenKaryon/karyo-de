// SPDX-License-Identifier: GPL-3.0-or-later
#include "alarm.h"

#include <QApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>

namespace {

const QString Service = QStringLiteral("org.freedesktop.Notifications");
const QString Path = QStringLiteral("/org/freedesktop/Notifications");
const QString Sound = QStringLiteral("/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga");
constexpr int MaxRings = 20;  // a minute

} // namespace

Alarm::Alarm(const QString &name, qint64 finishedAt, bool ring, QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(Service, Path, Service, QStringLiteral("ActionInvoked"), this, SLOT(onActionInvoked(uint, QString)));
    bus.connect(Service, Path, Service, QStringLiteral("NotificationClosed"), this, SLOT(onClosed(uint, uint)));
    QDBusInterface notifications(Service, Path, Service, bus);
    const QString time =
        QLocale().toString(QDateTime::fromMSecsSinceEpoch(finishedAt).time(), QLocale::ShortFormat);
    // Critical: it stays on screen until it is answered.
    const QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("karyo-clock")},
                            {QStringLiteral("urgency"), QVariant::fromValue(uchar(2))},
                            {QStringLiteral("category"), QStringLiteral("x-openkaryon.timer")}};
    const QDBusReply<uint> reply = notifications.call(
        QStringLiteral("Notify"), tr("Clock"), 0u, QStringLiteral("karyo-clock"), tr("“%1” is done").arg(name),
        tr("The timer finished at %1.").arg(time),
        QStringList{QStringLiteral("default"), tr("Show"), QStringLiteral("stop"), tr("Stop"),
                    QStringLiteral("restart"), tr("Restart")}, hints, 0);
    if (reply.isValid())
        m_id = reply.value();

    if (ring) {
        m_repeat.setInterval(3000);
        connect(&m_repeat, &QTimer::timeout, this, &Alarm::ring);
        m_repeat.start();
        this->ring();
    }
}

Alarm::~Alarm() { stop(); }

void Alarm::ring()
{
    if (m_stopped || ++m_rings > MaxRings) {
        m_repeat.stop();
        return;
    }
    QString player = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
    if (player.isEmpty())
        player = QStandardPaths::findExecutable(QStringLiteral("paplay"));
    if (player.isEmpty() || !QFileInfo::exists(Sound)) {
        QApplication::beep();
        return;
    }
    if (m_player && m_player->state() != QProcess::NotRunning)
        return;
    if (!m_player)
        m_player = new QProcess(this);
    m_player->start(player, {Sound});
}

void Alarm::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_repeat.stop();
    if (m_player && m_player->state() != QProcess::NotRunning)
        m_player->kill();
    if (m_id != 0) {
        QDBusInterface notifications(Service, Path, Service, QDBusConnection::sessionBus());
        notifications.call(QDBus::NoBlock, QStringLiteral("CloseNotification"), m_id);
    }
}

void Alarm::onActionInvoked(uint id, const QString &action)
{
    if (id != m_id || m_stopped)
        return;
    // The banner's close button only tucks it into the notification
    // center, so Stop is a button of its own.
    if (action == QLatin1String("restart"))
        Q_EMIT restartRequested();
    else if (action == QLatin1String("stop"))
        Q_EMIT dismissed();
    else
        Q_EMIT showRequested();
}

void Alarm::onClosed(uint id, uint)
{
    if (id != m_id || m_stopped)
        return;
    // Dismissed in the notification: answered.
    Q_EMIT dismissed();
}
