// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class AppCenterWidgetsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { QCoreApplication::setQuitLockEnabled(false); }
    void cardDetailsAndFailedInstallRecover()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray original = qgetenv("PATH");
        qputenv("QA_SOFTWARE", root.path().toUtf8());
        qputenv("XDG_DATA_HOME", root.filePath("data").toUtf8());
        qputenv("XDG_DATA_DIRS", root.filePath("system").toUtf8());
        QVERIFY(QDir().mkpath(root.filePath("bin")));
        QFile fixture(QFINDTESTDATA("fixtures/tool.py"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto bytes = fixture.readAll();
        for (const auto &name : QStringList { "pkexec", "apt-cache", "apt-get", "dpkg-query", "flatpak" })
        {
            QFile tool(root.filePath("bin/" + name));
            QVERIFY(tool.open(QIODevice::WriteOnly));
            QCOMPARE(tool.write(bytes), bytes.size());
            tool.close();
            QVERIFY(tool.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        }
        qputenv("PATH", root.filePath("bin").toUtf8() + ':' + original);
        QFile deny(root.filePath("deny-job"));
        QVERIFY(deny.open(QIODevice::WriteOnly));
        deny.close();
        PackageManager manager;
        CatalogApp app;
        app.id = "qa-new";
        app.name = "New App";
        app.kind = "cli";
        app.source = "apt";
        app.package = "qa-new";
        app.summary = "A public fixture";
        AppCard card(app, &manager);
        card.resize(300, 200);
        card.show();
        auto *action = card.findChild<Pill *>();
        QVERIFY(action);
        QVERIFY(action->isEnabled());
        QCOMPARE(action->text(), QString("Install"));
        QSignalSpy details(&card, &AppCard::detailsRequested);
        QTest::mouseClick(&card, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCOMPARE(details.size(), 1);
        QSignalSpy failed(&manager, &PackageManager::jobFailed);
        action->click();
        QVERIFY(manager.isBusy(app));
        QVERIFY(!action->isEnabled());
        QTRY_COMPARE(failed.size(), 1);
        QTRY_VERIFY(!manager.isBusy(app));
        QVERIFY(action->isEnabled());
        QCOMPARE(action->text(), QString("Install"));
        auto reaped = []
        {
            for (auto *p : QCoreApplication::instance()->findChildren<QProcess *>())
                if (p->state() != QProcess::NotRunning)
                    return false;
            return true;
        };
        QTRY_VERIFY(reaped());
        qputenv("PATH", original);
        qunsetenv("QA_SOFTWARE");
    }
};
QTEST_MAIN(AppCenterWidgetsTest)
#include "widgettest.moc"
