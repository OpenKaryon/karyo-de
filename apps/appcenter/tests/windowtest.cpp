// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcenterwindow.h"
#include "pages.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProcess>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <karyo/ui/navigation.h>
#include <karyo/ui/theme.h>
#include <memory>
namespace Ui = Karyo::Ui;
class AppCenterWindowTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_path = qgetenv("PATH"), m_home = qgetenv("HOME");
    void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
private Q_SLOTS:
    void initTestCase() { QCoreApplication::setQuitLockEnabled(false); }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        qputenv("HOME", m_root->filePath("home").toUtf8());
        QVERIFY(QDir().mkpath(m_root->filePath("home")));
        qputenv("XDG_DATA_HOME", m_root->filePath("data").toUtf8());
        qputenv("XDG_DATA_DIRS", m_root->filePath("system").toUtf8());
        qputenv("QA_SOFTWARE", m_root->path().toUtf8());
        qputenv("PATH", m_root->filePath("bin").toUtf8() + ':' + m_path);
        QFile fixture(QFINDTESTDATA("fixtures/tool.py"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto bytes = fixture.readAll();
        for (const auto &name :
            QStringList { "apt-cache", "apt-get", "dpkg-query", "flatpak", "du", "pkexec" })
        {
            const auto path = m_root->filePath("bin/" + name);
            put(path, bytes);
            QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        }
        QJsonObject app { { "id", "qa-new" }, { "name", "QA App" }, { "source", "apt" },
            { "package", "qa-new" }, { "summary", "A public fixture" }, { "kind", "cli" } };
        put(m_root->filePath("data/karyo/catalog.json"),
            QJsonDocument(QJsonObject { { "apps", QJsonArray { app } } }).toJson());
    }
    void cleanup()
    {
        auto reaped = []
        {
            for (auto *process : QCoreApplication::instance()->findChildren<QProcess *>())
                if (process->state() != QProcess::NotRunning)
                    return false;
            return true;
        };
        QTRY_VERIFY(reaped());
        qputenv("PATH", m_path);
        qputenv("HOME", m_home);
        qunsetenv("QA_SOFTWARE");
    }
    void routesSearchAndSelectingCurrentCategory()
    {
        AppCenterWindow window;
        window.resize(1280, 800);
        window.show();
        auto *nav = window.findChild<Ui::NavList *>("appcenterNavigation");
        auto *stack = window.findChild<QStackedWidget *>("appcenterPages");
        auto *edit = window.findChild<QWidget *>("appcenterSearch")->findChild<QLineEdit *>();
        QVERIFY(nav && stack && edit);
        const QStringList routes { "discover", "bio", "apps", "cli", "environments", "updates", "installed" };
        for (int i = 0; i < routes.size(); ++i)
        {
            window.showSection(routes.at(i));
            QCOMPARE(nav->currentIndex(), i);
            QCOMPARE(stack->currentIndex(), i);
        }
        window.showSection("unknown");
        QCOMPARE(nav->currentIndex(), 6);
        edit->setText("qa");
        edit->returnPressed();
        QCOMPARE(stack->currentIndex(), 7);
        nav->activated(6);
        QCOMPARE(stack->currentIndex(), 6);
        edit->returnPressed();
        QCOMPARE(stack->currentIndex(), 7);
        edit->clear();
        QCOMPARE(stack->currentIndex(), 6);
    }
    void detailsBackAndThemePreserveSearchAndModel()
    {
        AppCenterWindow window;
        auto *manager = window.findChild<PackageManager *>("appcenterPackages");
        auto *edit = window.findChild<QWidget *>("appcenterSearch")->findChild<QLineEdit *>();
        auto *back = window.findChild<QAbstractButton *>("appcenterBack");
        edit->setText("qa");
        edit->returnPressed();
        window.showApp("qa-new");
        auto *stack = window.findChild<QStackedWidget *>("appcenterPages");
        QVERIFY(stack->currentWidget()->findChild<DetailsPage *>());
        QPointer<QStackedWidget> old(stack);
        Q_EMIT Ui::ThemeNotifier::instance()->changed();
        QTRY_VERIFY(old.isNull());
        QCOMPARE(window.findChild<PackageManager *>("appcenterPackages"), manager);
        stack = window.findChild<QStackedWidget *>("appcenterPages");
        QVERIFY(stack->currentWidget()->findChild<DetailsPage *>());
        back->click();
        QCOMPARE(stack->currentIndex(), 7);
        back->click();
        QCOMPARE(stack->currentIndex(), 0);
        window.showApp("unknown-app");
        auto *status = window.findChild<QLabel *>("appcenterStatus");
        QVERIFY(status->text().contains("not in the current catalog"));
        manager->queryFailed("<b>Literal failure</b>");
        QCOMPARE(status->text(), QString("<b>Literal failure</b>"));
        QCOMPARE(status->textFormat(), Qt::PlainText);
        manager->install(manager->catalog().first());
        QVERIFY(manager->isBusy(manager->catalog().first()));
        QVERIFY(status->text().isEmpty());
        QTRY_VERIFY(!manager->isBusy(manager->catalog().first()));
        QVERIFY(status->text().isEmpty());
    }
};
QTEST_MAIN(AppCenterWindowTest)
#include "windowtest.moc"
