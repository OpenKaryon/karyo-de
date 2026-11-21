// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

namespace Karyo::Ui {

namespace {

const QString Nm = QStringLiteral("org.freedesktop.NetworkManager");
const QString NmPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString Active = Nm + QStringLiteral(".Connection.Active");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
using Settings = QMap<QString, QVariantMap>;

QString tr(const char *source)
{
    return QCoreApplication::translate("Wifi", source);
}

QDBusMessage request(const QString &path, const QString &interface, const char *method)
{
    auto message = QDBusMessage::createMethodCall(Nm, path, interface, QString::fromLatin1(method));
    message.setInteractiveAuthorizationAllowed(true);
    return message;
}

class WifiActivation : public QObject
{
public:
    WifiActivation(QObject *context, std::function<void(const QString &)> finished)
        : QObject(QCoreApplication::instance()), m_context(context), m_finished(std::move(finished))
    {
    }

    void start(const QString &interface, const QString &accessPoint, WifiSecurity security, const QString &password)
    {
        qDBusRegisterMetaType<Settings>();
        Settings settings;
        if (security != WifiSecurity::Open) {
            settings[QStringLiteral("802-11-wireless-security")] = {
                {QStringLiteral("key-mgmt"), security == WifiSecurity::Sae ? QStringLiteral("sae") : QStringLiteral("wpa-psk")},
                {QStringLiteral("psk"), password}
            };
        }
        auto device = request(NmPath, Nm, "GetDeviceByIpIface");
        device << interface;
        call(device, [this, settings, accessPoint](const QDBusMessage &reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) {
                complete(reply.errorMessage());
                return;
            }
            m_device = reply.arguments().value(0).value<QDBusObjectPath>().path();
            auto activate = request(NmPath, Nm, "AddAndActivateConnection");
            activate << QVariant::fromValue(settings) << reply.arguments().value(0)
                     << QVariant::fromValue(QDBusObjectPath(accessPoint));
            call(activate, [this](const QDBusMessage &result) {
                if (result.type() == QDBusMessage::ErrorMessage) {
                    complete(result.errorMessage());
                    return;
                }
                m_profile = result.arguments().value(0).value<QDBusObjectPath>().path();
                m_active = result.arguments().value(1).value<QDBusObjectPath>().path();
                m_elapsed.start();
                checkState();
            });
        });
    }

private:
    void call(const QDBusMessage &message, std::function<void(const QDBusMessage &)> done)
    {
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 30000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done] {
            watcher->deleteLater();
            done(watcher->reply());
        });
    }

    void checkState()
    {
        if (m_active.isEmpty()) {
            complete(tr("NetworkManager did not return a connection."));
            return;
        }
        auto state = request(m_active, Properties, "Get");
        state << Active << QStringLiteral("State");
        call(state, [this](const QDBusMessage &result) {
            const uint state = result.arguments().value(0).value<QDBusVariant>().variant().toUInt();
            // NetworkManager active-connection states: 2 activated, 4 deactivated.
            if (state == 2) {
                complete({});
            } else if (state == 4 || result.type() == QDBusMessage::ErrorMessage) {
                reportFailure();
            } else if (m_elapsed.elapsed() >= 45000) {
                complete(tr("The network took too long to connect."));
            } else {
                QTimer::singleShot(400, this, [this] { checkState(); });
            }
        });
    }

    void reportFailure()
    {
        auto reason = request(m_device, Properties, "Get");
        reason << Nm + QStringLiteral(".Device") << QStringLiteral("StateReason");
        call(reason, [this](const QDBusMessage &result) {
            const auto value = result.arguments().value(0).value<QDBusVariant>().variant();
            uint state = 0;
            uint code = 0;
            if (value.canConvert<QDBusArgument>()) {
                const auto pair = value.value<QDBusArgument>();
                pair.beginStructure();
                pair >> state >> code;
                pair.endStructure();
            }
            // NMDeviceStateReason: missing secrets, supplicant failure, DHCP/IP failure.
            if (code == 7)
                complete(tr("The network rejected or could not obtain the password."));
            else if (code == 5 || (code >= 15 && code <= 17))
                complete(tr("The network did not provide an IP address."));
            else if (code >= 8 && code <= 11)
                complete(tr("Wi-Fi authentication failed. Check the password and signal."));
            else
                complete(tr("Connection failed. Check the password and signal, then try again."));
        });
    }

    void complete(const QString &error)
    {
        // Remove only the profile this attempt created, so retrying uses the new password.
        if (!error.isEmpty() && !m_profile.isEmpty() && m_profile != QLatin1String("/")) {
            auto remove = request(m_profile, Nm + QStringLiteral(".Settings.Connection"), "Delete");
            call(remove, [this, error](const QDBusMessage &) { finish(error); });
        } else {
            finish(error);
        }
    }

    void finish(const QString &error)
    {
        if (m_context)
            m_finished(error);
        deleteLater();
    }

    QPointer<QObject> m_context;
    std::function<void(const QString &)> m_finished;
    QString m_device;
    QString m_profile;
    QString m_active;
    QElapsedTimer m_elapsed;
};

} // namespace

WifiSecurity wifiSecurity(const QString &security)
{
    if (security.isEmpty() || security == QLatin1String("--") || security == QLatin1String("OWE"))
        return WifiSecurity::Open;
    if (security.contains(QLatin1String("802.1X")))
        return WifiSecurity::Unsupported;
    if (security.contains(QLatin1String("WPA2")) || security.contains(QLatin1String("WPA1")))
        return WifiSecurity::Personal;
    if (security.contains(QLatin1String("WPA3")))
        return WifiSecurity::Sae;
    return WifiSecurity::Unsupported;
}

bool validWifiPassword(WifiSecurity security, const QString &password)
{
    const qsizetype bytes = password.toUtf8().size();
    if (security == WifiSecurity::Sae)
        return bytes > 0 && bytes <= 63;
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    static const QRegularExpression passphrase(QStringLiteral("^[ -~]{8,63}$"));
    return passphrase.match(password).hasMatch() || hex.match(password).hasMatch();
}

QString wifiPasswordHint(WifiSecurity security)
{
    return security == WifiSecurity::Sae ? tr("Enter the WPA3 password.")
        : tr("Use 8–63 ASCII characters or a 64-digit hexadecimal key.");
}

void connectWifi(QObject *context, const QString &interface, const QString &accessPoint,
                 WifiSecurity security, const QString &password, std::function<void(const QString &)> finished)
{
    (new WifiActivation(context, std::move(finished)))->start(interface, accessPoint, security, password);
}

} // namespace Karyo::Ui
