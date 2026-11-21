// SPDX-License-Identifier: GPL-3.0-or-later
#include "places.h"
#include <karyo/ui/navigation.h>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMetaType>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDir>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using Interfaces = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, Interfaces>;
Q_DECLARE_METATYPE(Interfaces)
Q_DECLARE_METATYPE(ManagedObjects)
static const QString Base = "org.freedesktop.UDisks2";
static const QString Root = "/org/freedesktop/UDisks2";
static const QString BlockPath = Root + "/block_devices/qa_usb";

class FakeObjects : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")
public:
    ManagedObjects objects;
public Q_SLOTS:
    Q_SCRIPTABLE ManagedObjects GetManagedObjects() { return objects; }
Q_SIGNALS:
    void InterfacesAdded(const QDBusObjectPath &object, const Interfaces &interfaces);
};
class FakeFilesystem : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.UDisks2.Filesystem")
public:
    int requests = 0;
    QString mount;
    bool fail = false;
public Q_SLOTS:
    Q_SCRIPTABLE QString Mount(const QVariantMap &) {
        ++requests;
        setDelayedReply(true);
        const auto request = message(); const auto bus = connection();
        const bool denied = fail; const QString target = mount;
        QTimer::singleShot(60, this, [bus, request, denied, target] {
            bus.send(denied ? request.createErrorReply(Base + ".Error.NotAuthorized", "QA mount denied")
                            : request.createReply(QVariantList{target}));
        });
        return {};
    }
};
class PlacesTest : public QObject {
    Q_OBJECT
    FakeObjects m_objects;
    FakeFilesystem m_fs;
    QTemporaryDir m_root;
    void prepare(const QString &connectionBus = "usb") {
        m_fs.requests = 0; m_fs.fail = false; m_fs.mount = m_root.filePath("mounted");
        QVERIFY(QDir().mkpath(m_fs.mount));
        const QDBusObjectPath drive(Root + "/drives/qa");
        const Interfaces block = {
            {Base + ".Block", {{"Drive", QVariant::fromValue(drive)}, {"Device", QByteArray("/dev/qa_usb\0", 12)}, {"IdLabel", "QA USB"}}},
            {Base + ".Filesystem", {{"MountPoints", QVariant::fromValue(QList<QByteArray>{})}}}
        };
        m_objects.objects = {{QDBusObjectPath(BlockPath), block},
                             {drive, {{Base + ".Drive", {{"ConnectionBus", connectionBus}}}}}};
    }
private Q_SLOTS:
    void initTestCase() {
        qDBusRegisterMetaType<Interfaces>(); qDBusRegisterMetaType<ManagedObjects>();
        qDBusRegisterMetaType<QList<QByteArray>>();
        qputenv("XDG_CONFIG_HOME", m_root.filePath("config").toUtf8());
        auto bus = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS"), "qa-udisks-service");
        QVERIFY(bus.isConnected());
        QVERIFY(bus.registerService(Base));
        QVERIFY(bus.registerObject(Root, &m_objects, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportAllSignals));
        QVERIFY(bus.registerObject(BlockPath, &m_fs, QDBusConnection::ExportScriptableSlots));
    }
    void explicitMountAndHotplug() {
        prepare();
        Places places; places.show();
        QTRY_COMPARE(places.m_usbVolumes.size(), 1);
        QCOMPARE(m_fs.requests, 0); // Discovery never mounts a user's drive.
        QSignalSpy navigate(&places, &Places::navigate);
        const int row = places.m_paths.indexOf(":mount:" + BlockPath);
        QVERIFY(row >= 0);
        places.m_list->activated(row); places.m_list->activated(row);
        QTRY_COMPARE(navigate.size(), 1);
        QCOMPARE(m_fs.requests, 1);
        QCOMPARE(navigate.first().first().toString(), m_fs.mount);
        m_objects.objects.clear();
        m_objects.InterfacesAdded(QDBusObjectPath(BlockPath), {});
        QTRY_VERIFY(places.m_usbVolumes.isEmpty());
    }
    void filteringAndMountError() {
        prepare("ata");
        Places places; places.show();
        QTRY_VERIFY(!places.m_usbRefreshPending);
        QVERIFY(places.m_usbVolumes.isEmpty());
        prepare(); places.refreshUsb();
        QTRY_COMPARE(places.m_usbVolumes.size(), 1);
        m_fs.fail = true;
        QSignalSpy navigate(&places, &Places::navigate);
        places.m_list->activated(places.m_paths.indexOf(":mount:" + BlockPath));
        QTRY_COMPARE(m_fs.requests, 1);
        QTRY_VERIFY(!places.findChildren<QMessageBox *>().isEmpty());
        QCOMPARE(places.findChild<QMessageBox *>()->text(), QString("QA mount denied"));
        QCOMPARE(navigate.size(), 0);
        QVERIFY(places.m_mounting.isEmpty());
        places.findChild<QMessageBox *>()->close();
    }
    void breadcrumbAndPinPersistence() {
        Breadcrumb breadcrumb; breadcrumb.resize(500, 34); breadcrumb.show();
        breadcrumb.setPath(m_root.path()); breadcrumb.editPath();
        auto *editor = breadcrumb.findChild<QLineEdit *>();
        QCOMPARE(editor->selectedText(), m_root.path());
        QSignalSpy navigate(&breadcrumb, &Breadcrumb::navigate);
        editor->setText(m_root.path()); QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(navigate.size(), 1); QCOMPARE(navigate.first().first().toString(), m_root.path());
        { Places places; places.pin(m_root.path()); places.pin(m_root.path()); QCOMPARE(places.pinned().count(m_root.path()), 1); }
        Places restored; QVERIFY(restored.pinned().contains(m_root.path())); restored.unpin(m_root.path());
        QVERIFY(!restored.pinned().contains(m_root.path()));
    }
};
QTEST_MAIN(PlacesTest)
#include "placestest.moc"
