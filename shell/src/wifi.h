// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <functional>

class QObject;

namespace Karyo::Ui {

enum class WifiSecurity { Open, Personal, Sae, Unsupported };

WifiSecurity wifiSecurity(const QString &security);
bool validWifiPassword(WifiSecurity security, const QString &password);
QString wifiPasswordHint(WifiSecurity security);
void connectWifi(QObject *context, const QString &interface, const QString &accessPoint,
                 WifiSecurity security, const QString &password,
                 std::function<void(const QString &error)> finished);

} // namespace Karyo::Ui
