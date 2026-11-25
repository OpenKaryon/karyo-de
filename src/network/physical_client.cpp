// SPDX-License-Identifier: GPL-3.0-or-later
#include "physical_client.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QPointer>

namespace Karyo::Ui {
namespace {
void call(QObject *context, const QString &method, const QVariantList &arguments, int timeout,
          std::function<void(const QJsonObject &, const QString &)> finished)
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.karyo.PhysicalNetwork"),
        QStringLiteral("/org/karyo/PhysicalNetwork"), QStringLiteral("org.karyo.PhysicalNetwork1"), method);
    message.setArguments(arguments);
    message.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, timeout), QCoreApplication::instance());
    const QPointer<QObject> alive(context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
        [alive, finished = std::move(finished)](QDBusPendingCallWatcher *w) {
            QDBusPendingReply<QString> reply = *w;
            w->deleteLater();
            if (!alive)
                return;
            if (reply.isError()) {
                finished({}, reply.error().message());
                return;
            }
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(reply.value().toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()
                || document.object().value(QStringLiteral("version")).toInt() != 1) {
                finished({}, QCoreApplication::translate("PhysicalNetwork", "The network service returned an invalid response."));
                return;
            }
            finished(document.object(), {});
        });
}
}

bool physicalNetworkManaged()
{
    // A service-selection hint only. The system bus authenticates every request.
    return !qEnvironmentVariable("KARYO_NETWORK_ENDPOINT").isEmpty();
}

void readPhysicalNetwork(QObject *context, std::function<void(const QJsonObject &, const QString &)> finished)
{
    call(context, QStringLiteral("GetState"), {}, 10000,
         [finished = std::move(finished)](const QJsonObject &value, const QString &error) {
             if (error.isEmpty() && value.value(QStringLiteral("owner")).toString().isEmpty()) {
                 finished({}, QCoreApplication::translate("PhysicalNetwork", "The physical network service needs an update."));
                 return;
             }
             finished(value, error);
         });
}

void changePhysicalNetwork(QObject *context, QJsonObject request, std::function<void(const QString &)> finished)
{
    if (!request.contains(QStringLiteral("owner"))) {
        readPhysicalNetwork(context, [context, request, finished = std::move(finished)](const QJsonObject &value, const QString &error) mutable {
            if (!error.isEmpty()) { finished(error);return; }
            request.insert(QStringLiteral("owner"), value.value(QStringLiteral("owner")));
            changePhysicalNetwork(context, request, std::move(finished));
        });
        return;
    }
    request.insert(QStringLiteral("version"), 1);
    call(context, QStringLiteral("Change"), {QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact))},
         140000, [finished = std::move(finished)](const QJsonObject &value, const QString &error) {
             finished(!error.isEmpty() ? error : value.value(QStringLiteral("success")).toBool()
                 ? QString() : QCoreApplication::translate("PhysicalNetwork", "The network change could not finish."));
         });
}
}
