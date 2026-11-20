// SPDX-License-Identifier: GPL-3.0-or-later
#include "systemcontrols.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString DefaultSink = QStringLiteral("@DEFAULT_AUDIO_SINK@");

QSettings shellSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("shell"));
}

int readInt(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().toInt() : 0;
}

} // namespace

// ---------------------------------------------------------------- AudioControl

AudioControl::AudioControl(QObject *parent)
    : QObject(parent)
{
    m_available = !QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty();
    // A dragged slider sends many values; wpctl gets the latest every 60ms.
    m_throttle.setSingleShot(true);
    m_throttle.setInterval(60);
    connect(&m_throttle, &QTimer::timeout, this, [this] {
        if (m_pendingVolume < 0)
            return;
        run({QStringLiteral("set-volume"), DefaultSink, QString::number(m_pendingVolume / 100.0, 'f', 2)});
        m_pendingVolume = -1;
    });
    if (m_available)
        read();
}

void AudioControl::run(const QStringList &args, const std::function<void(const QString &)> &done)
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done](int code) {
        process->deleteLater();
        if (done && code == 0)
            done(QString::fromUtf8(process->readAllStandardOutput()));
    });
    process->start(QStringLiteral("wpctl"), args);
}

void AudioControl::read()
{
    // "Volume: 0.40" or "Volume: 0.40 [MUTED]"
    run({QStringLiteral("get-volume"), DefaultSink}, [this](const QString &out) {
        const auto match = QRegularExpression(QStringLiteral("Volume:\\s*([0-9.]+)")).match(out);
        if (!match.hasMatch()) {
            m_available = false;
            Q_EMIT changed();
            return;
        }
        m_volume = qRound(match.captured(1).toDouble() * 100);
        m_muted = out.contains(QLatin1String("[MUTED]"));
        Q_EMIT changed();
    });
    run({QStringLiteral("inspect"), DefaultSink}, [this](const QString &out) {
        const auto match = QRegularExpression(QStringLiteral("node\\.description = \"([^\"]*)\"")).match(out);
        if (match.hasMatch()) {
            m_outputName = match.captured(1);
            Q_EMIT changed();
        }
    });
}

void AudioControl::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
    m_pendingVolume = m_volume;
    if (!m_throttle.isActive())
        m_throttle.start();
}

void AudioControl::toggleMute()
{
    m_muted = !m_muted;
    run({QStringLiteral("set-mute"), DefaultSink, QStringLiteral("toggle")});
    Q_EMIT changed();
}

// ---------------------------------------------------------------- BrightnessControl

BrightnessControl::BrightnessControl(QObject *parent)
    : QObject(parent)
{
    const QDir dir(QStringLiteral("/sys/class/backlight"));
    const QStringList devices = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (devices.isEmpty())
        return;
    m_device = devices.first();
    m_max = readInt(dir.filePath(m_device + QStringLiteral("/max_brightness")));
    m_value = readInt(dir.filePath(m_device + QStringLiteral("/brightness")));
}

int BrightnessControl::brightness() const { return m_max > 0 ? qRound(m_value * 100.0 / m_max) : 0; }

void BrightnessControl::setBrightness(int percent)
{
    if (!isAvailable())
        return;
    // Never fully dark: the screen would look switched off.
    m_value = qMax(1, qRound(qBound(0, percent, 100) * m_max / 100.0));
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1/session/auto"),
        QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("SetBrightness"));
    call << QStringLiteral("backlight") << m_device << uint(m_value);
    QDBusConnection::systemBus().asyncCall(call);
}

// ---------------------------------------------------------------- BluetoothControl

namespace {
const QString BluezService = QStringLiteral("org.bluez");
const QString BluezAdapter = QStringLiteral("/org/bluez/hci0");
} // namespace

BluetoothControl::BluetoothControl(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.interface() || !bus.interface()->isServiceRegistered(BluezService))
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(BluezService, BluezAdapter, Properties, QStringLiteral("Get"));
    call << QStringLiteral("org.bluez.Adapter1") << QStringLiteral("Powered");
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError())
            return;
        m_available = true;
        m_powered = reply.value().variant().toBool();
        Q_EMIT changed();
    });
}

void BluetoothControl::setPowered(bool powered)
{
    QDBusMessage call = QDBusMessage::createMethodCall(BluezService, BluezAdapter, Properties, QStringLiteral("Set"));
    call << QStringLiteral("org.bluez.Adapter1") << QStringLiteral("Powered")
         << QVariant::fromValue(QDBusVariant(powered));
    QDBusConnection::systemBus().asyncCall(call);
    m_powered = powered;
    Q_EMIT changed();
}

// ---------------------------------------------------------------- PowerProfileControl

PowerProfileControl::PowerProfileControl(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.interface())
        return;
    // power-profiles-daemon 0.20 moved to UPower's name; older ones use hadess.
    if (bus.interface()->isServiceRegistered(QStringLiteral("org.freedesktop.UPower.PowerProfiles"))) {
        m_service = QStringLiteral("org.freedesktop.UPower.PowerProfiles");
        m_path = QStringLiteral("/org/freedesktop/UPower/PowerProfiles");
    } else if (bus.interface()->isServiceRegistered(QStringLiteral("net.hadess.PowerProfiles"))) {
        m_service = QStringLiteral("net.hadess.PowerProfiles");
        m_path = QStringLiteral("/net/hadess/PowerProfiles");
    } else {
        return;
    }
    m_interface = m_service;
    read();
}

void PowerProfileControl::read()
{
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, Properties, QStringLiteral("GetAll"));
    call << m_interface;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError())
            return;
        m_profile = reply.value().value(QStringLiteral("ActiveProfile")).toString();
        m_profiles.clear();
        // Profiles: aa{sv}, each with a "Profile" name.
        const QDBusArgument profiles = reply.value().value(QStringLiteral("Profiles")).value<QDBusArgument>();
        profiles.beginArray();
        while (!profiles.atEnd()) {
            QVariantMap entry;
            profiles >> entry;
            m_profiles.append(entry.value(QStringLiteral("Profile")).toString());
        }
        profiles.endArray();
        Q_EMIT changed();
    });
}

void PowerProfileControl::cycle()
{
    static const QStringList order = {QStringLiteral("balanced"), QStringLiteral("performance"),
                                      QStringLiteral("power-saver")};
    int index = int(order.indexOf(m_profile));
    QString next;
    for (int i = 1; i <= order.size() && next.isEmpty(); ++i) {
        const QString candidate = order.at((index + i) % order.size());
        if (m_profiles.contains(candidate))
            next = candidate;
    }
    if (next.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, Properties, QStringLiteral("Set"));
    call << m_interface << QStringLiteral("ActiveProfile") << QVariant::fromValue(QDBusVariant(next));
    QDBusConnection::systemBus().asyncCall(call);
    m_profile = next;
    Q_EMIT changed();
}

QString PowerProfileControl::label(const QString &profile)
{
    if (profile == QLatin1String("performance"))
        return tr("Performance");
    if (profile == QLatin1String("power-saver"))
        return tr("Power saver");
    return tr("Balanced");
}

// ---------------------------------------------------------------- NightLight

NightLight::NightLight(QObject *parent)
    : QObject(parent)
{
    if (isAvailable() && shellSettings().value(QStringLiteral("QuickSettings/nightLight")).toBool())
        start();
}

NightLight::~NightLight() { stop(); }

bool NightLight::isAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("wlsunset")).isEmpty();
}

void NightLight::setOn(bool on)
{
    if (on == isOn())
        return;
    on ? start() : stop();
    shellSettings().setValue(QStringLiteral("QuickSettings/nightLight"), on);
    Q_EMIT changed();
}

void NightLight::start()
{
    m_process = new QProcess(this);
    // Day and night temperatures almost equal: a steady warm light.
    m_process->start(QStringLiteral("wlsunset"), {QStringLiteral("-t"), QStringLiteral("4000"),
                                                   QStringLiteral("-T"), QStringLiteral("4001")});
}

void NightLight::stop()
{
    if (!m_process)
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(1000))
        m_process->kill();
    delete m_process;
}
