// SPDX-License-Identifier: GPL-3.0-or-later
#include "inventory.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>

class GreeterInventoryTest : public QObject
{
    Q_OBJECT
    static void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
    static QByteArray bytes(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
private Q_SLOTS:
    void mainAccountsOnlyAndInvalidMappingStopsDiscovery()
    {
        const auto map = Karyo::Accounts::DesktopAccounts::parse("owner work experiments\nother lab\n");
        const QList<GreeterUser> records { { "root", "Root", "/bin/sh", 0 },
            { "owner", "Owner", "/bin/bash", 1000 }, { "work", "Work", "/bin/bash", 1001 },
            { "experiments", "Experiments", "/bin/bash", 1002 }, { "other", "Other", "/bin/sh", 1003 },
            { "lab", "Lab", "/bin/sh", 1004 }, { "independent", "Independent", "", 1010 },
            { "disabled", "", "/usr/sbin/nologin", 1011 }, { "false", "", "/bin/false", 1012 },
            { "nobody", "", "/bin/sh", 65534 }, { "owner", "Duplicate", "/bin/sh", 1000 } };
        QString error;
        const auto users = visibleGreeterUsers(records, map, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(users.size(), 3);
        QCOMPARE(users.at(0).login, QString("owner"));
        QCOMPARE(users.at(1).login, QString("other"));
        QCOMPARE(users.at(2).login, QString("independent"));
        const auto invalid = Karyo::Accounts::DesktopAccounts::parse("owner work\nother work\n");
        QVERIFY(visibleGreeterUsers(records, invalid, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void sessionsKeepArgumentsAndCoexistence()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString program = root.filePath("session launcher");
        put(program, "#!/bin/sh\nexit 0\n");
        QVERIFY(QFile::setPermissions(program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray entry
            = QString("[Desktop Entry]\nType=Application\nName=Public Session\nExec=\"%1\" --flag \"literal "
                      "argument\"\nDesktopNames=Karyo;wlroots;\n")
                  .arg(program)
                  .toUtf8();
        put(root.filePath("z-other.desktop"), entry);
        put(root.filePath("openkaryon.desktop"), entry);
        put(root.filePath("karyo.desktop"), entry);
        put(root.filePath("hidden.desktop"), entry + "Hidden=true\n");
        put(root.filePath("nodisplay.desktop"), entry + "NoDisplay=true\n");
        put(root.filePath("missing.desktop"),
            "[Desktop Entry]\nType=Application\nName=Missing\nExec=/missing/session\n");
        put(root.filePath("tryexec.desktop"), entry + "TryExec=/missing/optional-session\n");
        put(root.filePath("invalid-desktops.desktop"), entry + "DesktopNames=Karyo;bad value;\n");
        const auto sessions = greeterSessions(root.path());
        QCOMPARE(sessions.size(), 3);
        QCOMPARE(sessions.at(0).id, QString("karyo"));
        QCOMPARE(sessions.at(1).id, QString("openkaryon"));
        QCOMPARE(sessions.at(2).id, QString("z-other"));
        QCOMPARE(sessions.first().command, QStringList({ program, "--flag", "literal argument" }));
        QCOMPARE(sessions.first().desktopNames, QString("Karyo:wlroots"));
        QCOMPARE(sessions.first().environment(),
            QStringList({ "XDG_SESSION_TYPE=wayland", "XDG_SESSION_DESKTOP=karyo",
                "XDG_CURRENT_DESKTOP=Karyo:wlroots" }));
    }
    void selectionCompatibilityAndAtomicPublication()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto config = root.filePath("config"), home = root.filePath("home");
        const auto legacy = home + "/openkaryon-greeter.conf";
        const auto current = config + "/karyo/greeter.json";
        put(legacy, "[General]\nlastUser=owner\nlastSession=openkaryon\n");
        auto selected = readGreeterSelection(config, home);
        QVERIFY(selected.error.isEmpty());
        QCOMPARE(selected.user, QString("owner"));
        QCOMPARE(selected.session, QString("openkaryon"));
        QString error;
        QVERIFY(saveGreeterSelection("other", "karyo", &error, config));
        QVERIFY(error.isEmpty());
        selected = readGreeterSelection(config, home);
        QCOMPARE(selected.user, QString("other"));
        QCOMPARE(selected.session, QString("karyo"));
        const auto before = bytes(current);
        QVERIFY(!saveGreeterSelection(QString(QChar::Null), "karyo", &error, config));
        QVERIFY(!saveGreeterSelection("bad\nuser", "karyo", &error, config));
        QVERIFY(!saveGreeterSelection("owner", "bad\rsession", &error, config));
        QVERIFY(!error.isEmpty());
        QCOMPARE(bytes(current), before);
        QVERIFY(!QFile::permissions(current).testAnyFlags(
            QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther));
        QCOMPARE(bytes(legacy), QByteArray("[General]\nlastUser=owner\nlastSession=openkaryon\n"));
        put(root.filePath("blocked"), "file");
        QVERIFY(!saveGreeterSelection("owner", "karyo", &error, root.filePath("blocked")));
        QCOMPARE(bytes(current), before);
    }
    void invalidPrimaryNeverRevivesLegacySelection()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto config = root.filePath("config"), home = root.filePath("home");
        const auto current = config + "/karyo/greeter.json";
        put(home + "/openkaryon-greeter.conf", "[General]\nlastUser=owner\nlastSession=openkaryon\n");
        put(current, "{ invalid JSON");
        auto selected = readGreeterSelection(config, home);
        QVERIFY(!selected.error.isEmpty());
        QVERIFY(selected.user.isEmpty());
        put(current, QByteArray(4097, 'x'));
        QVERIFY(!readGreeterSelection(config, home).error.isEmpty());
        QVERIFY(QFile::remove(current));
        QVERIFY(
            QFile::link(root.filePath("missing"), current)); // Dangling symlinks are authoritative errors.
        QVERIFY(!readGreeterSelection(config, home).error.isEmpty());
        QVERIFY(QFile::remove(current));
        QCOMPARE(::mkfifo(QFile::encodeName(current).constData(), 0600), 0);
        QVERIFY(!readGreeterSelection(config, home).error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(GreeterInventoryTest)
#include "greeterinventorytest.moc"
