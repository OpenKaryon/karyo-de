// SPDX-License-Identifier: GPL-3.0-or-later
#include "command.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
using Karyo::Apps::runCommand;
class CommandTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void shellArgumentsAreLiteral()
    {
        const QString value = QStringLiteral("path with ' quotes; $(exit 8)\nnext line");
        QProcess process;
        process.start("/bin/sh", {"-c", "printf '%s' " + Karyo::Apps::shellQuote(value)});
        QVERIFY(process.waitForFinished(2000));
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(QString::fromUtf8(process.readAllStandardOutput()), value);
    }
    void successAndExitFailure()
    {
        QObject context;
        int successes = 0, failures = 0;
        QString output, error;
        runCommand(
            &context, "/bin/sh", { "-c", "printf 'query output'" },
            [&](const QString &text)
            {
                ++successes;
                output = text;
            },
            [&](const QString &) { ++failures; });
        QTRY_COMPARE(successes, 1);
        QCOMPARE(output, QString("query output"));
        QCOMPARE(failures, 0);
        runCommand(
            &context, "/bin/sh", { "-c", "printf denied >&2; exit 7" }, [&](const QString &) { ++successes; },
            [&](const QString &text)
            {
                ++failures;
                error = text;
            });
        QTRY_COMPARE(failures, 1);
        QCOMPARE(error, QString("denied"));
        QCOMPARE(successes, 1);
    }
    void missingCommandAndTimeout()
    {
        QObject context;
        int successes = 0, failures = 0;
        QString error;
        runCommand(
            &context, "/nonexistent/karyo-query", { }, [&](const QString &) { ++successes; },
            [&](const QString &text)
            {
                ++failures;
                error = text;
            });
        QTRY_COMPARE(failures, 1);
        QVERIFY(!error.isEmpty());
        QCOMPARE(successes, 0);
        int ticks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start();
        runCommand(
            &context, "/bin/sleep", { "5" }, [&](const QString &) { ++successes; },
            [&](const QString &text)
            {
                ++failures;
                error = text;
            },
            120);
        QTRY_COMPARE(failures, 2);
        QVERIFY(error.contains("timed out"));
        QVERIFY(ticks > 2);
        QCOMPARE(successes, 0);
        QTest::qWait(20);
        QCOMPARE(failures, 2);
    }
    void excessiveOutputAndDestroyedContext()
    {
        QObject context;
        int calls = 0;
        QString error;
        runCommand(
            &context, "/usr/bin/head", { "-c", "18000000", "/dev/zero" }, [&](const QString &) { ++calls; },
            [&](const QString &text)
            {
                ++calls;
                error = text;
            });
        QTRY_COMPARE(calls, 1);
        QVERIFY(error.contains("too much data"));
        QTemporaryDir root;
        const QString pidFile = root.filePath("pid");
        auto *temporary = new QObject;
        runCommand(
            temporary, "/bin/sh", { "-c", "echo $$ > \"$1\"; exec /bin/sleep 5", "fixture", pidFile },
            [&](const QString &) { ++calls; }, [&](const QString &) { ++calls; });
        QTRY_VERIFY(QFileInfo::exists(pidFile));
        QFile file(pidFile);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray pid = file.readAll().trimmed();
        QVERIFY(!pid.isEmpty());
        const QString proc = QString("/proc/%1").arg(QString::fromLatin1(pid));
        QVERIFY(QFileInfo::exists(proc));
        delete temporary;
        QTRY_VERIFY(!QFileInfo::exists(proc));
        QTest::qWait(20);
        QCOMPARE(calls, 1);
        const QString marker = root.filePath("must-not-run");
        auto *neverStarted = new QObject;
        runCommand(
            neverStarted, "/usr/bin/touch", { marker }, [&](const QString &) { ++calls; },
            [&](const QString &) { ++calls; });
        delete neverStarted;
        QTest::qWait(40);
        QVERIFY(!QFileInfo::exists(marker));
        QCOMPARE(calls, 1);
    }
    void stableLanguageWithoutOverwritingParentEnvironment()
    {
        const QByteArray original = qgetenv("LC_ALL");
        qputenv("LC_ALL", "ru_RU.UTF-8");
        QObject context;
        QString output;
        int calls = 0;
        runCommand(&context, "/usr/bin/printenv", { "LC_ALL" },
            [&](const QString &text)
            {
                output = text.trimmed();
                ++calls;
            });
        QTRY_COMPARE(calls, 1);
        QCOMPARE(output, QString("C.UTF-8"));
        QCOMPARE(qgetenv("LC_ALL"), QByteArray("ru_RU.UTF-8"));
        qputenv("LC_ALL", original);
    }
};
QTEST_GUILESS_MAIN(CommandTest)
#include "commandtest.moc"
