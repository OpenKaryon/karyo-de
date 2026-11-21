// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusObjectPath>
#include <QMap>
#include <QVariantMap>
namespace Karyo::Bluetooth
{
using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;
}
