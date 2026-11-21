// SPDX-License-Identifier: GPL-3.0-or-later
#include "defaultapps.h"
#include "pages.h"
#include "widgets.h"
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class DefaultAppsTest : public QObject {
    Q_OBJECT
    static void put(const QString &path, const QByteArray &data) {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
    }
    static QByteArray read(const QString &path) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
private Q_SLOTS:
    void atomicEntriesPreserveOtherSections() {
        QTemporaryDir root; qputenv("XDG_CONFIG_HOME", root.path().toUtf8());
        const QString path = root.filePath("mimeapps.list");
        put(path, "# comment\n[Default Applications]\ntext/plain=old.desktop;\nimage/png=viewer.desktop;\n[Added Associations]\ntext/plain=other.desktop;\n[Default Applications]\ntext/plain=duplicate.desktop;\n");
        QString error;
        QVERIFY2(DefaultApps::set({"text/plain", "x-scheme-handler/https"}, "new.desktop", &error), qPrintable(error));
        const auto data = read(path); QVERIFY(data.contains("# comment"));
        QVERIFY(data.contains("image/png=viewer.desktop;")); QVERIFY(data.contains("text/plain=other.desktop;"));
        QCOMPARE(data.count("text/plain=new.desktop;"), 1); QVERIFY(!data.contains("old.desktop")); QVERIFY(!data.contains("duplicate.desktop"));
        QVERIFY(data.contains("x-scheme-handler/https=new.desktop;"));
        const auto original = data;
        QVERIFY(!DefaultApps::set({"text/plain\ninjected"}, "new.desktop", &error));
        QVERIFY(!DefaultApps::set({"text/plain\n"}, "new.desktop", &error));
        QVERIFY(!DefaultApps::set({"text/plain"}, "../bad.desktop", &error)); QCOMPARE(read(path), original);
        QVERIFY(QFile::remove(path)); QVERIFY(QDir().mkdir(path));
        QVERIFY(!DefaultApps::set({"text/plain"}, "new.desktop", &error)); QVERIFY(!error.isEmpty());
    }
    void applicationPageWritesSelectedDefaults() {
        QTemporaryDir root; const QString config = root.filePath("config"), data = root.filePath("data");
        qputenv("XDG_CONFIG_HOME", config.toUtf8()); qputenv("XDG_DATA_HOME", data.toUtf8()); qputenv("XDG_DATA_DIRS", data.toUtf8());
        for (const QString &name : {QString("Alpha"), QString("Beta")})
            put(data + "/applications/" + name + ".desktop", QString("[Desktop Entry]\nType=Application\nName=%1\nExec=/bin/true %u\nMimeType=x-scheme-handler/http;x-scheme-handler/https;text/html;\n").arg(name).toUtf8());
        std::unique_ptr<SettingsPage> page(createAppsPage(nullptr));
        SettingRow *browser = nullptr;
        for (auto *row : page->findChildren<SettingRow *>())
            if (row->property("settingTitle").toString() == "Web browser") browser = row;
        QVERIFY(browser); auto *choices = browser->findChild<QComboBox *>(); QVERIFY(choices); QCOMPARE(choices->count(), 2);
        const int beta = choices->findData("Beta.desktop"); QVERIFY(beta >= 0);
        choices->setCurrentIndex(beta); choices->activated(beta);
        const auto profile = read(config + "/mimeapps.list");
        QVERIFY(profile.contains("x-scheme-handler/https=Beta.desktop;")); QVERIFY(profile.contains("text/html=Beta.desktop;"));
        std::unique_ptr<SettingsPage> accounts(createAccountsPage(nullptr)); QVERIFY(!accounts->findChildren<FactsCard *>().isEmpty());
    }
};
QTEST_MAIN(DefaultAppsTest)
#include "defaultappstest.moc"
