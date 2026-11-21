// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
namespace NetworkDetails
{
struct Connection
{
    QString name, device, type, ipv4, gateway, dns, ipv6, mac;
    bool external = false;
    bool valid() const { return !device.isEmpty(); }
};
struct Route
{
    QString device, gateway;
    int metric = 0;
};
QStringList splitTerse(const QString &line);
Route primaryRoute(const QString &table);
QString nameServers(const QString &resolvConf);
Connection fromNmcli(const QString &output, const QString &preferredInterface = { });
// Read-only observation in the caller's namespace. Never changes the host's
// network manager; also works with networkd/ifupdown and unmanaged interfaces.
Connection systemConnection();
}
