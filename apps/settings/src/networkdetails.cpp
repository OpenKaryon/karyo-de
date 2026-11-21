// SPDX-License-Identifier: GPL-3.0-or-later
#include "networkdetails.h"
#include <QFile>
#include <QHostAddress>
#include <QMap>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <limits>

namespace NetworkDetails
{
QStringList splitTerse(const QString &line)
{
    QStringList fields;
    QString field;
    for (qsizetype i = 0; i < line.size(); ++i)
    {
        if (line.at(i) == QLatin1Char('\\') && i + 1 < line.size())
            field += line.at(++i);
        else if (line.at(i) == QLatin1Char(':'))
        {
            fields.append(field);
            field.clear();
        }
        else
            field += line.at(i);
    }
    fields.append(field);
    return fields;
}
Route primaryRoute(const QString &table)
{
    Route route;
    int best = std::numeric_limits<int>::max();
    for (const auto &line : table.split(QLatin1Char('\n')))
    {
        const auto fields = line.simplified().split(QLatin1Char(' '));
        if (fields.size() < 8 || fields.at(1) != QLatin1String("00000000")
            || fields.at(0) == QLatin1String("lo"))
            continue;
        bool flagsOk = false, metricOk = false, gatewayOk = false;
        const auto flags = fields.at(3).toUInt(&flagsOk, 16);
        const int metric = fields.at(6).toInt(&metricOk);
        const quint32 gateway = fields.at(2).toUInt(&gatewayOk, 16);
        if (!flagsOk || !(flags & 1) || !metricOk || metric < 0 || !gatewayOk || metric >= best)
            continue;
        best = metric;
        route.device = fields.at(0);
        route.metric = metric;
        const quint32 address = ((gateway & 0xff) << 24) | ((gateway & 0xff00) << 8)
            | ((gateway & 0xff0000) >> 8) | ((gateway >> 24) & 0xff);
        route.gateway = gateway ? QHostAddress(address).toString() : QString();
    }
    return route;
}
QString nameServers(const QString &resolvConf)
{
    QStringList servers;
    for (const auto &line : resolvConf.split(QLatin1Char('\n')))
    {
        const auto fields = line.section(QLatin1Char('#'), 0, 0).simplified().split(QLatin1Char(' '));
        if (fields.size() < 2 || fields.first() != QLatin1String("nameserver"))
            continue;
        if (!QHostAddress(fields.at(1)).isNull() && !servers.contains(fields.at(1)))
            servers.append(fields.at(1));
    }
    return servers.join(QStringLiteral(", "));
}
Connection fromNmcli(const QString &output, const QString &preferredInterface)
{
    QList<Connection> connections;
    QMap<QString, QString> device;
    const auto flush = [&]
    {
        if (device.value(QStringLiteral("GENERAL.STATE")).section(QLatin1Char(' '), 0, 0)
            != QLatin1String("100"))
            return;
        const auto name = device.value(QStringLiteral("GENERAL.DEVICE")),
                   type = device.value(QStringLiteral("GENERAL.TYPE"));
        if (name.isEmpty() || name == QLatin1String("lo") || type == QLatin1String("loopback"))
            return;
        Connection info;
        info.device = name;
        info.type = type;
        info.name = device.value(QStringLiteral("GENERAL.CONNECTION"));
        info.ipv4
            = device.value(QStringLiteral("IP4.ADDRESS[1]"), device.value(QStringLiteral("IP4.ADDRESS")));
        info.ipv6
            = device.value(QStringLiteral("IP6.ADDRESS[1]"), device.value(QStringLiteral("IP6.ADDRESS")));
        info.gateway = device.value(QStringLiteral("IP4.GATEWAY"));
        info.dns = device.value(QStringLiteral("IP4.DNS"));
        info.mac = device.value(QStringLiteral("GENERAL.HWADDR"));
        connections.append(info);
    };
    for (const auto &line : output.split(QLatin1Char('\n')))
    {
        if (line.isEmpty())
        {
            flush();
            device.clear();
            continue;
        }
        const auto fields = splitTerse(line);
        const auto key = fields.value(0), value = fields.mid(1).join(QLatin1Char(':'));
        if (key == QLatin1String("IP4.DNS") || key.startsWith(QLatin1String("IP4.DNS[")))
        {
            auto &dns = device[QStringLiteral("IP4.DNS")];
            dns += dns.isEmpty() ? value : QStringLiteral(", ") + value;
        }
        else
            device[key] = value;
    }
    flush();
    for (const auto &info : connections)
        if (info.device == preferredInterface)
            return info;
    for (const auto &info : connections)
        if (info.type != QLatin1String("bridge"))
            return info;
    return { };
}
namespace
{
    QString read(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return { };
        return QString::fromUtf8(file.read(256 * 1024));
    }
}
Connection systemConnection()
{
    const auto route = primaryRoute(read(QStringLiteral("/proc/net/route")));
    QList<Connection> candidates;
    const auto dns = nameServers(read(QStringLiteral("/etc/resolv.conf")));
    for (const auto &interface : QNetworkInterface::allInterfaces())
    {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        Connection info;
        info.external = true;
        info.dns = dns;
        info.device = interface.name();
        info.name = interface.humanReadableName();
        info.type = QStringLiteral("system");
        info.mac = interface.hardwareAddress();
        for (const auto &entry : interface.addressEntries())
        {
            if (entry.ip().isNull() || entry.ip().isLoopback())
                continue;
            QString value = entry.ip().toString();
            if (entry.prefixLength() >= 0)
                value += QLatin1Char('/') + QString::number(entry.prefixLength());
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && info.ipv4.isEmpty())
                info.ipv4 = value;
            if (entry.ip().protocol() == QAbstractSocket::IPv6Protocol && info.ipv6.isEmpty())
                info.ipv6 = value;
        }
        if (info.ipv4.isEmpty() && info.ipv6.isEmpty())
            continue;
        if (info.device == route.device)
        {
            info.gateway = route.gateway;
            info.dns = nameServers(read(QStringLiteral("/etc/resolv.conf")));
            return info;
        }
        candidates.append(info);
    }
    return candidates.isEmpty() ? Connection { } : candidates.first();
}
}
