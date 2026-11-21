// SPDX-License-Identifier: GPL-3.0-or-later
#include "blueztypes.h"
#include "pages.h"
#include "widgets.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QtTest>
#include <memory>

class FakeBluez : public QDBusVirtualObject
{
    Q_OBJECT
public:
    bool adapter = true, powered = true, connected = false, denied = false;
    int changes = 0;
    QString introspect(const QString &) const override { return { }; }
    bool handleMessage(const QDBusMessage &call, const QDBusConnection &bus) override
    {
        using namespace Karyo::Bluetooth;
        if (call.member() == "GetManagedObjects")
        {
            ObjectMap objects;
            if (adapter)
                objects.insert(QDBusObjectPath("/org/bluez/hci0"),
                    InterfaceMap {
                        { "org.bluez.Adapter1", { { "Powered", powered }, { "Alias", "QA Adapter" } } } });
            objects.insert(QDBusObjectPath("/org/bluez/hci0/dev_01"),
                InterfaceMap { { "org.bluez.Device1",
                    { { "Paired", true }, { "Connected", connected }, { "Alias", "QA Headset" } } } });
            bus.send(call.createReply({ QVariant::fromValue(objects) }));
            return true;
        }
        if (call.member() == "Set" || call.member() == "Connect" || call.member() == "Disconnect")
        {
            ++changes;
            const bool reject = denied;
            QTimer::singleShot(80, this,
                [this, call, bus, reject]
                {
                    if (reject)
                        bus.send(call.createErrorReply("org.bluez.Error.NotAuthorized", "Denied by QA"));
                    else
                    {
                        if (call.member() == "Set")
                            powered = call.arguments().at(2).value<QDBusVariant>().variant().toBool();
                        else
                            connected = call.member() == "Connect";
                        bus.send(call.createReply());
                    }
                });
            return true;
        }
        return false;
    }
};
class DevicesTest : public QObject
{
    Q_OBJECT
    FakeBluez m_bluez;
private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<Karyo::Bluetooth::InterfaceMap>();
        qDBusRegisterMetaType<Karyo::Bluetooth::ObjectMap>();
        auto bus = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS"), "qa-bluez");
        QVERIFY(bus.registerService("org.bluez"));
        QVERIFY(bus.registerVirtualObject("/", &m_bluez, QDBusConnection::SubPath));
    }
    void init()
    {
        m_bluez.adapter = true;
        m_bluez.powered = true;
        m_bluez.connected = false;
        m_bluez.denied = false;
        m_bluez.changes = 0;
    }
    void poweredStateDenialAndReadback()
    {
        std::unique_ptr<SettingsPage> page(createDevicesPage(nullptr));
        auto *power = page->findChild<ToggleSwitch *>("bluetoothPower");
        auto *status = page->findChild<QLabel *>("bluetoothStatus");
        QTRY_VERIFY(power->isEnabled());
        QVERIFY(power->isChecked());
        QCOMPARE(m_bluez.changes, 0);
        m_bluez.denied = true;
        power->click();
        QVERIFY(!power->isEnabled());
        QTRY_VERIFY(power->isEnabled());
        QVERIFY(power->isChecked());
        QVERIFY(m_bluez.powered);
        QVERIFY(status->text().contains("Denied by QA"));
        m_bluez.denied = false;
        power->click();
        QTRY_VERIFY(power->isEnabled());
        QVERIFY(!power->isChecked());
        QVERIFY(!m_bluez.powered);
        QVERIFY(status->text().isEmpty());
    }
    void pairedDeviceConnectAndAdapterRemoval()
    {
        std::unique_ptr<SettingsPage> page(createDevicesPage(nullptr));
        auto *power = page->findChild<ToggleSwitch *>("bluetoothPower");
        QTRY_VERIFY(power->isEnabled());
        auto *button = page->findChild<QPushButton *>();
        QVERIFY(button);
        QCOMPARE(button->text(), QString("Connect"));
        m_bluez.denied = true;
        button->click();
        QTRY_VERIFY(power->isEnabled());
        QVERIFY(!m_bluez.connected);
        QCOMPARE(page->findChild<QPushButton *>()->text(), QString("Connect"));
        m_bluez.denied = false;
        page->findChild<QPushButton *>()->click();
        QTRY_VERIFY(power->isEnabled());
        QVERIFY(m_bluez.connected);
        QCOMPARE(page->findChild<QPushButton *>()->text(), QString("Disconnect"));
        m_bluez.adapter = false;
        page->refresh();
        QTRY_VERIFY(!page->findChild<QLabel *>("bluetoothStatus")->text().contains("Denied"));
        QTest::qWait(50);
        QVERIFY(!power->isEnabled());
        bool noAdapter = false;
        for (auto *label : page->findChildren<QLabel *>())
            noAdapter |= label->text().contains("No Bluetooth adapter");
        QVERIFY(noAdapter);
    }
    void missingServiceRemainsReadable()
    {
        auto bus = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS"), "qa-bluez");
        QVERIFY(bus.unregisterService("org.bluez"));
        std::unique_ptr<SettingsPage> page(createDevicesPage(nullptr));
        QTRY_VERIFY(!page->findChild<QLabel *>("bluetoothStatus")->text().isEmpty());
        QVERIFY(!page->findChild<ToggleSwitch *>("bluetoothPower")->isEnabled());
        QVERIFY(bus.registerService("org.bluez"));
        page->refresh();
        QTRY_VERIFY(page->findChild<ToggleSwitch *>("bluetoothPower")->isEnabled());
    }
};
QTEST_MAIN(DevicesTest)
#include "devicestest.moc"
