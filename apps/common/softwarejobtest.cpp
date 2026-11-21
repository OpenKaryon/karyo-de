// SPDX-License-Identifier: GPL-3.0-or-later
#include "softwarejob.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <QtTest>
#include <memory>
using Karyo::Apps::SoftwareJob;
class SoftwareJobTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { QCoreApplication::setQuitLockEnabled(false); }
    void progressAndLifetimeSurviveObserverDestruction()
    {
        QTemporaryDir root;
        const QString marker = root.filePath("completed");
        auto *observer = new QObject;
        auto *job = SoftwareJob::start("/bin/sh",
            { "-c",
                "printf 'dlstatus:pkg:90:download\npmstatus:pkg:50:install\n'; sleep .2; printf done > "
                "\"$1\"",
                "fixture", marker });
        QVERIFY(SoftwareJob::hasActiveJobs());
        QSignalSpy progress(job, &SoftwareJob::progress);
        QSignalSpy done(job, &SoftwareJob::finished);
        bool callback = false;
        connect(job, &SoftwareJob::finished, observer, [&] { callback = true; });
        delete observer;
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.at(0).at(0).toBool());
        QVERIFY(QFileInfo::exists(marker));
        QVERIFY(!callback);
        QVERIFY(progress.size() >= 2);
        QCOMPARE(progress.last().at(0).toInt(), 100);
        QVERIFY(!SoftwareJob::hasActiveJobs());
    }
    void missingCommandCancellationAndFailureFinishExactlyOnce()
    {
        const QList<QPair<QString, QStringList>> cases { { "/missing/karyo-software-test", { } },
            { "/bin/sh", { "-c", "exit 126" } }, { "/bin/sh", { "-c", "exit 127" } } };
        for (const auto &test : cases)
        {
            auto *job = SoftwareJob::start(test.first, test.second);
            QSignalSpy done(job, &SoftwareJob::finished);
            QTRY_COMPARE(done.size(), 1);
            QVERIFY(!done.first().first().toBool());
            QVERIFY(!done.first().at(1).toString().isEmpty());
            QTest::qWait(20);
            QCOMPARE(done.size(), 1);
            QVERIFY(!SoftwareJob::hasActiveJobs());
        }
        auto *job = SoftwareJob::start("/bin/sh", { "-c", "echo 'Package failed' >&2; exit 2" });
        QSignalSpy done(job, &SoftwareJob::finished);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.first().at(1).toString(), QString("Package failed"));
    }
    void lastWindowClosureWaitsForTransaction()
    {
        QTemporaryDir root;
        const auto marker = root.filePath("completed-after-close");
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), { "--quit-lock-fixture", marker });
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitCode(), 0);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY(QFileInfo::exists(marker));
    }
    void retainedErrorIsBoundedWithoutKillingTransaction()
    {
        auto *job = SoftwareJob::start(
            "/bin/sh", { "-c", "head -c 1000000 /dev/zero >&2; printf '\nfinal error' >&2; exit 2" });
        QSignalSpy done(job, &SoftwareJob::finished);
        QTRY_COMPARE(done.size(), 1);
        const QString error = done.first().at(1).toString();
        QVERIFY(error.size() <= 256 * 1024);
        QVERIFY(error.endsWith("final error"));
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains("--quit-lock-fixture"))
    {
        QWidget window;
        window.show();
        SoftwareJob::start(
            "/bin/sh", { "-c", "sleep .25; printf done > \"$1\"", "fixture", app.arguments().last() });
        QTimer::singleShot(10, &window, &QWidget::close);
        return app.exec();
    }
    SoftwareJobTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "softwarejobtest.moc"
