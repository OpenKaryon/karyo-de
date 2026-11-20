// SPDX-License-Identifier: GPL-3.0-or-later
#include "notify/notificationserver.h"
#include "wifi.h"
#include "integrations.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

class ShellServiceTest : public QObject
{
    Q_OBJECT
private slots:
    void notificationPolicy()
    {
        NotificationServer server;
        QSignalSpy banners(&server, &NotificationServer::bannerRequested);
        QSignalSpy closed(&server, &NotificationServer::NotificationClosed);
        const auto notify = [&](uint replaces, const QVariantMap &hints = {}) {
            return server.Notify("fixture", replaces, {}, "Summary", "<b>Body</b>", {}, hints, 0);
        };
        const uint id = notify(0);
        QCOMPARE(banners.size(), 1);
        QCOMPARE(server.notifications().first().body, QStringLiteral("Body"));
        QCOMPARE(notify(id), id);
        QCOMPARE(server.notifications().size(), 1);
        server.setDoNotDisturb(true);
        notify(0);
        QCOMPARE(banners.size(), 2);
        notify(0, {{"urgency", 2}});
        QCOMPARE(banners.size(), 3);
        notify(0, {{"transient", true}});
        QCOMPARE(server.notifications().size(), 3);
        server.CloseNotification(id);
        QCOMPARE(closed.size(), 1);
        server.dismissAll();
        QVERIFY(server.notifications().isEmpty());
    }
    void wifiPasswordPolicy()
    {
        using namespace Karyo::Ui;
        QCOMPARE(wifiSecurity("WPA2"), WifiSecurity::Personal);
        QVERIFY(!validWifiPassword(WifiSecurity::Personal, "short"));
        QVERIFY(validWifiPassword(WifiSecurity::Personal, "eight888"));
        QVERIFY(!validWifiPassword(WifiSecurity::Personal, QString(64, 'z')));
        QVERIFY(validWifiPassword(WifiSecurity::Personal, QString(64, 'a')));
    }
    void optionalSettings()
    {
        const QByteArray before = qgetenv("PATH");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("PATH", directory.path().toUtf8());
        QVERIFY(Integrations::settingsProgram(false).isEmpty());
        QVERIFY(!Integrations::openSettings());
        QFile executable(directory.filePath("nm-connection-editor"));
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write("#!/bin/sh\nexit 0\n");
        executable.close();
        executable.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QCOMPARE(Integrations::settingsProgram(true), executable.fileName());
        QVERIFY(Integrations::settingsProgram(false).isEmpty());
        qputenv("PATH", before);
    }
};
QTEST_MAIN(ShellServiceTest)
#include "shellservicetest.moc"
