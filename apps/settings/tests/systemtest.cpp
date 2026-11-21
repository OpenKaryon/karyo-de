// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include "pages.h"
#include "widgets.h"
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

class FakeShell : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.karyo.Shell")
public:
    bool enabled = false, denied = false;
    int changes = 0;
public Q_SLOTS:
    Q_SCRIPTABLE bool DoNotDisturb() { return enabled; }
    Q_SCRIPTABLE void SetDoNotDisturb(bool on)
    {
        ++changes;
        setDelayedReply(true);
        const auto request = message();
        const auto bus = connection();
        const bool reject = denied;
        QTimer::singleShot(100, this,
            [this, request, bus, reject, on]
            {
                if (reject)
                    bus.send(
                        request.createErrorReply("org.freedesktop.DBus.Error.AccessDenied", "Denied by QA"));
                else
                {
                    enabled = on;
                    bus.send(request.createReply());
                }
            });
    }
};
class SystemTest : public QObject
{
    Q_OBJECT
    FakeShell m_shell;
    QByteArray m_path = qgetenv("PATH");
    std::unique_ptr<QTemporaryDir> m_root;
    static void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    static QByteArray display(double scale)
    {
        QJsonArray modes;
        modes.append(QJsonObject { { "width", 1280 }, { "height", 800 }, { "refresh", 60.0 },
            { "current", true }, { "preferred", true } });
        QJsonArray outputs;
        outputs.append(QJsonObject {
            { "name", "Virtual-1" }, { "enabled", true }, { "scale", scale }, { "modes", modes } });
        return QJsonDocument(outputs).toJson();
    }
    void script(const QString &name, const QByteArray &body)
    {
        const auto path = m_root->filePath("bin/" + name);
        put(path, "#!/bin/sh\n" + body);
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
private Q_SLOTS:
    void initTestCase()
    {
        const auto bus
            = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), "qa-shell");
        auto server = bus;
        QVERIFY(server.registerService("org.karyo.Shell"));
        QVERIFY(server.registerObject("/org/karyo/Shell", &m_shell, QDBusConnection::ExportScriptableSlots));
    }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        qputenv("XDG_CONFIG_HOME", m_root->filePath("config").toUtf8());
        qputenv("QA_STATE", m_root->path().toUtf8());
        qputenv("PATH", m_root->filePath("bin").toUtf8() + ':' + m_path);
        put(m_root->filePath("displays"), display(1.0));
        put(m_root->filePath("next-displays"), display(1.25));
        put(m_root->filePath("volume"), "Volume: 0.40\n");
        put(m_root->filePath("next-volume"), "Volume: 0.70\n");
        put(m_root->filePath("outputs"),
            "Audio\n Sinks:\n │ * 45. Speaker [vol: 0.40]\n │   46. Headset [vol: 0.40]\n Sources:\n");
        script("wlr-randr", R"(if [ "$1" = --json ]; then cat "$QA_STATE/displays"; exit; fi
sleep 0.06
if [ -f "$QA_STATE/deny-display" ]; then echo 'Display denied' >&2; exit 2; fi
cp "$QA_STATE/next-displays" "$QA_STATE/displays"
)");
        script("wpctl", R"(case "$1" in
get-volume) cat "$QA_STATE/volume" ;;
status) cat "$QA_STATE/outputs" ;;
set-volume) if [ -f "$QA_STATE/deny-sound" ]; then echo 'Audio denied' >&2; exit 2; fi
cp "$QA_STATE/next-volume" "$QA_STATE/volume" ;;
set-default) echo 'Output denied' >&2; exit 2 ;;
esac
)");
        m_shell.enabled = false;
        m_shell.denied = false;
        m_shell.changes = 0;
    }
    void cleanup() { qputenv("PATH", m_path); }
    void displayFailureNeverPersistsAndSuccessReadsBack()
    {
        std::unique_ptr<SettingsPage> page(createSystemPage(nullptr));
        QTRY_VERIFY(page->findChild<QComboBox *>("displayScale"));
        auto *scale = page->findChild<QComboBox *>("displayScale");
        QCOMPARE(scale->currentData().toDouble(), 1.0);
        put(m_root->filePath("deny-display"), "yes");
        scale->setCurrentIndex(1);
        scale->activated(1);
        auto *status = page->findChild<QLabel *>("displayStatus");
        QTRY_VERIFY(status->text().contains("Display denied"));
        QTRY_VERIFY(page->findChild<QComboBox *>("displayScale")->isEnabled());
        QCOMPARE(Config::desktop().value("Displays/Virtual-1/scale").toString(), QString());
        scale = page->findChild<QComboBox *>("displayScale");
        QCOMPARE(scale->currentData().toDouble(), 1.0);
        QVERIFY(QFile::remove(m_root->filePath("deny-display")));
        scale->setCurrentIndex(1);
        scale->activated(1);
        QTRY_COMPARE(Config::desktop().value("Displays/Virtual-1/scale").toString(), QString("1.25"));
        QTRY_VERIFY(page->findChild<QComboBox *>("displayScale")->isEnabled());
        QCOMPARE(page->findChild<QComboBox *>("displayScale")->currentData().toDouble(), 1.25);
        QVERIFY(status->text().isEmpty());
    }
    void soundFailureRollsBackAndAcceptedVolumeReadsBack()
    {
        std::unique_ptr<SettingsPage> page(createSystemPage(nullptr));
        auto *volume = page->findChild<QSlider *>("audioVolume");
        auto *outputs = page->findChild<QComboBox *>("audioOutput");
        QTRY_VERIFY(volume->isEnabled());
        QTRY_COMPARE(outputs->count(), 2);
        QCOMPARE(volume->value(), 40);
        put(m_root->filePath("deny-sound"), "yes");
        volume->setValue(80);
        auto *status = page->findChild<QLabel *>("soundStatus");
        QTRY_VERIFY(status->text().contains("Audio denied"));
        QTRY_VERIFY(volume->isEnabled());
        QCOMPARE(volume->value(), 40);
        outputs->setCurrentIndex(1);
        outputs->activated(1);
        QTRY_VERIFY(status->text().contains("Output denied"));
        QTRY_VERIFY(outputs->isEnabled());
        QCOMPARE(outputs->currentData().toString(), QString("45"));
        QVERIFY(QFile::remove(m_root->filePath("deny-sound")));
        volume->setValue(70);
        QTRY_COMPARE(volume->value(), 70);
        QTest::qWait(180);
        QTRY_VERIFY(volume->isEnabled());
        QVERIFY(status->text().isEmpty());
    }
    void publicShellContractDenialAndRecovery()
    {
        std::unique_ptr<SettingsPage> page(createSystemPage(nullptr));
        auto *dnd = page->findChild<ToggleSwitch *>("doNotDisturb");
        QTRY_VERIFY(dnd->isEnabled());
        QCOMPARE(m_shell.changes, 0);
        dnd->click();
        QVERIFY(!dnd->isEnabled());
        QTRY_VERIFY(dnd->isEnabled());
        QVERIFY(m_shell.enabled);
        QVERIFY(dnd->isChecked());
        m_shell.denied = true;
        dnd->click();
        QTRY_VERIFY(dnd->isEnabled());
        QVERIFY(dnd->isChecked());
        QVERIFY(page->findChild<QLabel *>("dndStatus")->text().contains("Denied by QA"));
        m_shell.denied = false;
        dnd->click();
        QTRY_VERIFY(dnd->isEnabled());
        QVERIFY(!dnd->isChecked());
        QVERIFY(page->findChild<QLabel *>("dndStatus")->text().isEmpty());
    }
    void unavailableShellDisablesOnlyNotifications()
    {
        auto bus
            = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), "qa-shell");
        QVERIFY(bus.unregisterService("org.karyo.Shell"));
        std::unique_ptr<SettingsPage> page(createSystemPage(nullptr));
        QTRY_VERIFY(page->findChild<QLabel *>("dndStatus")->text().contains("unavailable"));
        QVERIFY(!page->findChild<ToggleSwitch *>("doNotDisturb")->isEnabled());
        QTRY_VERIFY(page->findChild<QSlider *>("audioVolume")->isEnabled());
        QVERIFY(bus.registerService("org.karyo.Shell"));
        page->refresh();
        QTRY_VERIFY(page->findChild<ToggleSwitch *>("doNotDisturb")->isEnabled());
        QVERIFY(page->findChild<QLabel *>("dndStatus")->text().isEmpty());
    }
    void invalidDisplayReplyIsVisible()
    {
        put(m_root->filePath("displays"), "not json");
        std::unique_ptr<SettingsPage> page(createSystemPage(nullptr));
        QTRY_VERIFY(page->findChild<QLabel *>("displayStatus")->text().contains("invalid display list"));
        QVERIFY(!page->findChild<QComboBox *>("displayScale"));
    }
};
QTEST_MAIN(SystemTest)
#include "systemtest.moc"
