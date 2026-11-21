// SPDX-License-Identifier: GPL-3.0-or-later
#include "networkdetails.h"
#include <QtTest>
class NetworkDetailsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void loopbackAndBridgeNeverHideRealConnection()
    {
        const auto output = QString::fromUtf8(R"NM(GENERAL.DEVICE:lo
GENERAL.TYPE:loopback
GENERAL.STATE:100 (connected)
IP4.ADDRESS[1]:127.0.0.1/8

GENERAL.DEVICE:docker0
GENERAL.TYPE:bridge
GENERAL.STATE:100 (connected)

GENERAL.DEVICE:ens33
GENERAL.TYPE:ethernet
GENERAL.STATE:100 (connected)
GENERAL.CONNECTION:Wired
IP4.ADDRESS[1]:192.168.230.163/24
IP4.DNS[1]:1.1.1.1
IP4.DNS[2]:9.9.9.9
IP6.ADDRESS[1]:2001\:db8\:\:1/64

GENERAL.DEVICE:wlan0
GENERAL.TYPE:wifi
GENERAL.STATE:100 (connected)
GENERAL.CONNECTION:Office\:East
)NM");
        const auto wired = NetworkDetails::fromNmcli(output);
        QCOMPARE(wired.device, QString("ens33"));
        QCOMPARE(wired.dns, QString("1.1.1.1, 9.9.9.9"));
        QCOMPARE(wired.ipv6, QString("2001:db8::1/64"));
        const auto wifi = NetworkDetails::fromNmcli(output, "wlan0");
        QCOMPARE(wifi.device, QString("wlan0"));
        QCOMPARE(wifi.name, QString("Office:East"));
        QVERIFY(!NetworkDetails::fromNmcli(
            "GENERAL.DEVICE:lo\nGENERAL.TYPE:loopback\nGENERAL.STATE:100 (connected)\n")
                .valid());
    }
    void routeMetricAndResolverParsing()
    {
        const auto route = NetworkDetails::primaryRoute(
            "Iface Destination Gateway Flags RefCnt Use Metric Mask\nwlan0 00000000 0101A8C0 0003 0 0 100 "
            "00000000\nens33 00000000 02E6A8C0 0003 0 0 10 00000000\nbad 00000000 01010101 0000 0 0 0 "
            "00000000\n");
        QCOMPARE(route.device, QString("ens33"));
        QCOMPARE(route.gateway, QString("192.168.230.2"));
        QCOMPARE(route.metric, 10);
        QVERIFY(NetworkDetails::primaryRoute("bad data").device.isEmpty());
        QCOMPARE(NetworkDetails::nameServers(
                     "# config\nnameserver 127.0.0.53\nnameserver 2001:4860:4860::8888 # IPv6\nnameserver "
                     "invalid\nnameserver 127.0.0.53\nsearch example.test\n"),
            QString("127.0.0.53, 2001:4860:4860::8888"));
    }
};
QTEST_GUILESS_MAIN(NetworkDetailsTest)
#include "networkdetailstest.moc"
