// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>

class QObject;

namespace Karyo::Ui {
bool physicalNetworkManaged();
void readPhysicalNetwork(QObject *context, std::function<void(const QJsonObject &, const QString &)> finished);
void changePhysicalNetwork(QObject *context, QJsonObject request, std::function<void(const QString &)> finished);
}
