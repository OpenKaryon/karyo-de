// SPDX-License-Identifier: GPL-3.0-or-later
#include "physical_panel.h"
#include "physical_client.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTest>

class PhysicalFixture : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.karyo.PhysicalNetwork1")
public:
    QJsonObject state;
    QJsonObject last;
    QString failure;
public Q_SLOTS:
    QString GetState() const { return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)); }
    QString Change(const QString &text)
    {
        last = QJsonDocument::fromJson(text.toUtf8()).object();
        if (!failure.isEmpty()) {
            sendErrorReply(QStringLiteral("org.karyo.PhysicalNetwork1.Error"), failure);
            return {};
        }
        return QStringLiteral("{\"version\":1,\"success\":true}");
    }
};

class PhysicalNetworkTest : public QObject
{
    Q_OBJECT
    PhysicalFixture fixture;
private Q_SLOTS:
    void initTestCase()
    {
        qputenv("KARYO_NETWORK_ENDPOINT", "/run/karyo-test/client.json");
        auto bus = QDBusConnection::systemBus();
        QVERIFY(bus.registerService(QStringLiteral("org.karyo.PhysicalNetwork")));
        QVERIFY(bus.registerObject(QStringLiteral("/org/karyo/PhysicalNetwork"), &fixture, QDBusConnection::ExportAllSlots));
    }
    void passwordEntryAndNormalActivation()
    {
        fixture.last = {};
        fixture.state = {{"version", 1}, {"owner", ":1.30"}, {"wireless", true}, {"wireless_enabled", true},
            {"connections", QJsonArray()}, {"access_points", QJsonArray{QJsonObject{{"ssid", "Lab Wi-Fi"},
                {"security", "WPA2"}, {"interface", "wlan0"}, {"path", "/org/freedesktop/NetworkManager/AccessPoint/1"}}}}};
        Karyo::Ui::PhysicalNetworkPanel panel;
        panel.resize(360, 500);panel.show();
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("physicalConnections"));
        auto *button = panel.findChild<QPushButton *>(QStringLiteral("physicalConnect"));
        auto *password = panel.findChild<QLineEdit *>(QStringLiteral("physicalPassword"));
        QTRY_COMPARE(list->count(), 1);
        QVERIFY(button->isEnabled());button->click();
        QVERIFY(password->isVisible());QVERIFY(!button->isEnabled());
        password->setText(QStringLiteral("short"));QVERIFY(!button->isEnabled());
        password->setText(QStringLiteral("test-passphrase"));QVERIFY(button->isEnabled());
        button->click();
        QTRY_COMPARE(fixture.last.value(QStringLiteral("operation")).toString(), QStringLiteral("wifi"));
        QCOMPARE(fixture.last.value(QStringLiteral("owner")).toString(), QStringLiteral(":1.30"));
        QCOMPARE(fixture.last.value(QStringLiteral("interface")).toString(), QStringLiteral("wlan0"));
        QTRY_VERIFY(!password->isVisible());
        QCOMPARE(password->text(), QString());
    }
    void failedActivationKeepsThePasswordForRetry()
    {
        fixture.last = {};
        fixture.failure = QStringLiteral("Wi-Fi authentication failed. Check the password and signal.");
        Karyo::Ui::PhysicalNetworkPanel panel;
        panel.resize(360, 500);panel.show();
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("physicalConnections"));
        auto *button = panel.findChild<QPushButton *>(QStringLiteral("physicalConnect"));
        auto *password = panel.findChild<QLineEdit *>(QStringLiteral("physicalPassword"));
        auto *status = panel.findChild<QLabel *>(QStringLiteral("physicalStatus"));
        QTRY_COMPARE(list->count(), 1);
        button->click();password->setText(QStringLiteral("test-passphrase"));button->click();
        QTRY_COMPARE(status->text(), fixture.failure);
        QVERIFY(password->isVisible());
        QVERIFY(button->isEnabled());
        QCOMPARE(password->text(), QStringLiteral("test-passphrase"));
        fixture.failure.clear();
        button->click();
        QTRY_VERIFY(!password->isVisible());
        QCOMPARE(password->text(), QString());
    }
    void externalEthernetIsVisibleWithoutTakingItOver()
    {
        fixture.last = {};
        fixture.state = {{"version", 1}, {"owner", ":1.30"}, {"connected", true}, {"wireless", false},
            {"connections", QJsonArray{QJsonObject{{"name", "eth0"}, {"uuid", ""}, {"type", "802-3-ethernet"},
                {"active", true}, {"interface", "eth0"}, {"external", true}}}}, {"access_points", QJsonArray()}};
        Karyo::Ui::PhysicalNetworkPanel panel;
        panel.resize(360, 400);panel.show();
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("physicalConnections"));
        auto *button = panel.findChild<QPushButton *>(QStringLiteral("physicalConnect"));
        auto *status = panel.findChild<QLabel *>(QStringLiteral("physicalStatus"));
        QTRY_COMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains(QStringLiteral("eth0")));
        QVERIFY(!button->isEnabled());
        QVERIFY(status->text().contains(QStringLiteral("outside NetworkManager")));
        QVERIFY(fixture.last.isEmpty());
    }
    void olderServiceFailsOnceInsteadOfRetryingTheMutation()
    {
        fixture.last = {};
        fixture.state = {{"version", 1}, {"connected", true}};
        QObject context;
        bool done = false;
        QString message;
        Karyo::Ui::changePhysicalNetwork(&context, {{"operation", "radio"}, {"enabled", true}},
            [&](const QString &error) { message = error;done = true; });
        QTRY_VERIFY(done);
        QVERIFY(message.contains(QStringLiteral("update")));
        QVERIFY(fixture.last.isEmpty());
    }
};
QTEST_MAIN(PhysicalNetworkTest)
#include "physical_network_test.moc"
