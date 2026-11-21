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
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

class FakeTimedate : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.timedate1")
    Q_PROPERTY(bool NTP READ ntp)
    Q_PROPERTY(QString Timezone READ timezone)
public:
    bool enabled = true, denied = false, interactive = false;
    QString zone = "UTC";
    int requests = 0;
    bool ntp() const { return enabled; }
    QString timezone() const { return zone; }
    void respond(const std::function<void()> &apply) {
        ++requests;
        setDelayedReply(true);
        const auto request = message(); const auto bus = connection(); const bool reject = denied;
        QTimer::singleShot(120, this, [this, request, bus, reject, apply] {
            if (reject) bus.send(request.createErrorReply("org.freedesktop.DBus.Error.AccessDenied", "Denied by QA"));
            else { apply(); bus.send(request.createReply()); }
        });
    }
public Q_SLOTS:
    Q_SCRIPTABLE void SetNTP(bool on, bool allow) { interactive = allow && message().isInteractiveAuthorizationAllowed(); respond([this, on] { enabled = on; }); }
    Q_SCRIPTABLE void SetTimezone(const QString &name, bool allow) { interactive = allow && message().isInteractiveAuthorizationAllowed(); respond([this, name] { zone = name; }); }
};
class TimeLanguageTest : public QObject {
    Q_OBJECT
    FakeTimedate m_backend;
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_path = qgetenv("PATH");
    static void put(const QString &path, const QByteArray &data) {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
    }
    static QByteArray read(const QString &path) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
private Q_SLOTS:
    void initTestCase() {
        auto bus = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS"), "qa-timedate");
        QVERIFY(bus.registerService("org.freedesktop.timedate1"));
        QVERIFY(bus.registerObject("/org/freedesktop/timedate1", &m_backend,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
    }
    void init() {
        m_root = std::make_unique<QTemporaryDir>(); QVERIFY(m_root->isValid());
        qputenv("XDG_CONFIG_HOME", m_root->filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", m_root->filePath("data").toUtf8());
        qputenv("XKB_DEFAULT_LAYOUT", "us"); qputenv("XKB_DEFAULT_OPTIONS", "compose:ralt,grp:alt_shift_toggle");
        const QString locale = m_root->filePath("bin/locale");
        put(locale, "#!/bin/sh\nprintf '%s\\n' en_US.utf8 ru_RU.UTF-8\n");
        QVERIFY(QFile::setPermissions(locale, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("PATH", m_root->filePath("bin").toUtf8() + ':' + m_path);
        m_backend.enabled = true; m_backend.zone = "UTC"; m_backend.denied = false; m_backend.requests = 0;
    }
    void cleanup() { qputenv("PATH", m_path); }
    void authorizedChangesReadBackAndDeniedChangesRollback() {
        std::unique_ptr<SettingsPage> page(createTimeLanguagePage(nullptr)); page->show();
        auto *ntp = page->findChild<ToggleSwitch *>("automaticTime"); QVERIFY(ntp);
        auto *zones = page->findChild<QComboBox *>("timeZone"); QVERIFY(zones);
        auto *status = page->findChild<QLabel *>("timedateStatus"); QVERIFY(status);
        QTRY_VERIFY(ntp->isEnabled()); QVERIFY(ntp->isChecked()); QCOMPARE(zones->currentData().toString(), QString("UTC"));
        QCOMPARE(m_backend.requests, 0); // Reading never changes system time.
        int ticks = 0; QTimer heartbeat; heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; }); heartbeat.start();
        ntp->click(); QVERIFY(!ntp->isEnabled());
        QTRY_VERIFY(ntp->isEnabled()); QVERIFY(!ntp->isChecked()); QVERIFY(!m_backend.enabled); QVERIFY(ticks > 2);
        QCOMPARE(m_backend.requests, 1); QVERIFY(status->text().isEmpty()); QVERIFY(m_backend.interactive);
        m_backend.denied = true;
        const int paris = zones->findData("Europe/Paris"); QVERIFY(paris >= 0);
        zones->setCurrentIndex(paris); zones->activated(paris); QVERIFY(!zones->isEnabled());
        QTRY_VERIFY(zones->isEnabled()); QCOMPARE(zones->currentData().toString(), QString("UTC"));
        QCOMPARE(m_backend.zone, QString("UTC")); QVERIFY(status->text().contains("Denied by QA"));
        m_backend.denied = false;
        zones->setCurrentIndex(paris); zones->activated(paris);
        QTRY_VERIFY(zones->isEnabled()); QCOMPARE(m_backend.zone, QString("Europe/Paris"));
        QCOMPARE(zones->currentData().toString(), QString("Europe/Paris")); QVERIFY(status->text().isEmpty());
    }
    void languageLayoutsAndFailedProfileWrite() {
        std::unique_ptr<SettingsPage> page(createTimeLanguagePage(nullptr));
        auto *languages = page->findChild<QComboBox *>("displayLanguage"); QVERIFY(languages);
        QTRY_COMPARE(languages->count(), 2);
        const int russian = languages->findData("ru_RU.UTF-8"); QVERIFY(russian >= 0);
        languages->setCurrentIndex(russian); languages->activated(russian);
        QCOMPARE(Config::sessionValue("LANG"), QString("ru_RU.UTF-8"));
        auto *layouts = page->findChild<QComboBox *>("addKeyboardLayout"); QVERIFY(layouts);
        layouts->setCurrentIndex(layouts->findData("ru")); page->findChild<QPushButton *>("addLayout")->click();
        QCOMPARE(Config::sessionValue("XKB_DEFAULT_LAYOUT"), QString("us,ru"));
        auto *shortcut = page->findChild<QComboBox *>("layoutShortcut"); QVERIFY(shortcut);
        const int caps = shortcut->findData("grp:caps_toggle"); shortcut->setCurrentIndex(caps); shortcut->activated(caps);
        QCOMPARE(Config::sessionValue("XKB_DEFAULT_OPTIONS"), QString("compose:ralt,grp:caps_toggle"));
        const QString profile = m_root->filePath("config/karyo/session.env");
        QVERIFY(QFile::remove(profile)); QVERIFY(QDir().mkdir(profile));
        const int english = languages->findData("en_US.UTF-8"); languages->setCurrentIndex(english); languages->activated(english);
        QCOMPARE(languages->currentIndex(), russian);
        QVERIFY(page->findChild<QLabel *>("sessionSettingStatus")->text().contains("Could not save"));
    }
    void privacyHistoryPublicationAndFailure() {
        const QString history = m_root->filePath("data/recently-used.xbel");
        put(history, "old recent history");
        std::unique_ptr<SettingsPage> page(createPrivacyPage(nullptr));
        auto *clear = page->findChild<QPushButton *>("clearRecentHistory"); QVERIFY(clear); clear->click();
        QVERIFY(!clear->isEnabled()); QVERIFY(read(history).contains("<xbel")); QVERIFY(!read(history).contains("old recent"));
        QVERIFY(QFile::remove(history)); QVERIFY(QDir().mkdir(history));
        std::unique_ptr<SettingsPage> failed(createPrivacyPage(nullptr));
        auto *retry = failed->findChild<QPushButton *>("clearRecentHistory"); retry->click(); QVERIFY(retry->isEnabled());
        bool error = false;
        for (auto *label : failed->findChildren<QLabel *>()) error |= label->text().contains("Could not clear history");
        QVERIFY(error); QVERIFY(QFileInfo(history).isDir());
    }
    void unavailableTimedatedLeavesLanguageUsable() {
        auto bus = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SYSTEM_BUS_ADDRESS"), "qa-timedate");
        QVERIFY(bus.unregisterService("org.freedesktop.timedate1"));
        std::unique_ptr<SettingsPage> page(createTimeLanguagePage(nullptr));
        auto *status = page->findChild<QLabel *>("timedateStatus");
        QTRY_VERIFY(status->text().contains("unavailable"));
        QVERIFY(!page->findChild<ToggleSwitch *>("automaticTime")->isEnabled());
        QVERIFY(!page->findChild<QComboBox *>("timeZone")->isEnabled());
        auto *language = page->findChild<QComboBox *>("displayLanguage");
        QTRY_VERIFY(language->isEnabled());
        QVERIFY(bus.registerService("org.freedesktop.timedate1"));
    }

};
QTEST_MAIN(TimeLanguageTest)
#include "timelanguagetest.moc"
