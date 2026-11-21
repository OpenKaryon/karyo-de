// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class SettingsConfigTest : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    QString path(const QString &name) const { return m_root->filePath(name); }
    static void put(const QString &name, const QByteArray &data) {
        QVERIFY(QDir().mkpath(QFileInfo(name).absolutePath()));
        QFile file(name); QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
    static QByteArray read(const QString &name) {
        QFile file(name); if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private Q_SLOTS:
    void init() {
        m_root = std::make_unique<QTemporaryDir>(); QVERIFY(m_root->isValid());
        qputenv("XDG_CONFIG_HOME", m_root->path().toUtf8());
    }
    void legacyMigrationPreservesUnknownValues() {
        const auto legacy = path("openkaryon/session.env");
        const QByteArray original("# custom comment\nLANG=\"en_US.UTF-8\"\nCUSTOM='keep me'\nXKB_DEFAULT_LAYOUT=us\n");
        put(legacy, original);
        QCOMPARE(Config::sessionValue("LANG"), QString("en_US.UTF-8"));
        QString error;
        QVERIFY2(Config::setSessionValue("XKB_DEFAULT_LAYOUT", "us,ru", &error), qPrintable(error));
        QCOMPARE(read(legacy), original);
        QVERIFY(read(path("karyo/session.env")).contains("CUSTOM='keep me'"));
        QVERIFY(read(path("karyo/session.env")).contains("# custom comment"));
        QCOMPARE(Config::sessionValue("XKB_DEFAULT_LAYOUT"), QString("us,ru"));
        put(legacy, "LANG=old\n");
        QCOMPARE(Config::sessionValue("LANG"), QString("en_US.UTF-8"));
    }
    void existingPublicProfileAndDuplicateAssignments() {
        put(path("openkaryon/session.env"), "LANG=legacy\n");
        put(path("karyo/session.env"), "# preserve\nLANG=first\nexport LANG=\"last\"\nXKB_DEFAULT_OPTIONS=grp:alt_shift_toggle\n");
        QCOMPARE(Config::sessionValue("LANG"), QString("last"));
        QVERIFY(Config::setSessionValue("LANG", "ru_RU.UTF-8"));
        QCOMPARE(Config::sessionValue("LANG"), QString("ru_RU.UTF-8"));
        const auto profile = read(path("karyo/session.env"));
        QCOMPARE(profile.count("LANG="), 1);
        QVERIFY(profile.contains("XKB_DEFAULT_OPTIONS=grp:alt_shift_toggle"));
        QVERIFY(Config::setSessionValue("LANG", {}));
        QVERIFY(!read(path("karyo/session.env")).contains("LANG="));
        QVERIFY(Config::sessionValue("LANG").isEmpty());
    }
    void valuesRemainLiteralWhenTheSessionSourcesThem() {
        const QString marker = path("must-not-exist");
        const QString value = QString("$(touch %1) 'single' \"double\" $HOME \\literal").arg(marker);
        QVERIFY(Config::setSessionValue("KARYO_TEST_VALUE", value));
        QCOMPARE(Config::sessionValue("KARYO_TEST_VALUE"), value);
        QProcess shell;
        shell.start("/bin/sh", {"-c", ". \"$1\"; printf '%s' \"$KARYO_TEST_VALUE\"", "fixture", path("karyo/session.env")});
        QVERIFY(shell.waitForFinished()); QCOMPARE(shell.exitCode(), 0);
        QCOMPARE(QString::fromUtf8(shell.readAllStandardOutput()), value);
        QVERIFY(!QFileInfo::exists(marker));
    }
    void invalidAndFailedWritesPreserveData() {
        const auto profile = path("karyo/session.env"); put(profile, "LANG=keep\n");
        const QByteArray original = read(profile); QString error;
        QVERIFY(!Config::setSessionValue("INVALID;key", "value", &error)); QVERIFY(!error.isEmpty());
        QVERIFY(!Config::setSessionValue("LANG", "new\ninjected", &error));
        QCOMPARE(read(profile), original);
        QVERIFY(QFile::remove(profile)); QVERIFY(QDir().mkdir(profile));
        QVERIFY(!Config::setSessionValue("LANG", "new", &error)); QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo(profile).isDir());
    }
    void lookAndShellSettingsKeepCompatiblePaths() {
        QVERIFY(Config::desktop().fileName().endsWith("/openkaryon/desktop.conf"));
        QVERIFY(Config::shell().fileName().endsWith("/openkaryon/shell.conf"));
        QCOMPARE(Config::initials(QString::fromUtf8("Данил Тест")), QString::fromUtf8("ДТ"));
        QCOMPARE(Config::initials("  One   Two Three  "), QString("OT"));
        QVERIFY(!Config::userLogin().isEmpty());
    }
};
QTEST_GUILESS_MAIN(SettingsConfigTest)
#include "configtest.moc"
