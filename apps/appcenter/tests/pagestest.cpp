// SPDX-License-Identifier: GPL-3.0-or-later
#include "pages.h"
#include "widgets.h"
#include "softwarejob.h"
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class PagesTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_path = qgetenv("PATH"), m_home = qgetenv("HOME");
    void put(const QString &path, const QByteArray &bytes)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    static QString labels(QWidget *page)
    {
        QStringList result;
        for (auto *label : page->findChildren<QLabel *>())
            result.append(label->text());
        return result.join('\n');
    }
    static Pill *button(QWidget *page, const QString &text)
    {
        for (auto *pill : page->findChildren<Pill *>())
            if (pill->text() == text)
                return pill;
        return nullptr;
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
        QFile fixture(QFINDTESTDATA("fixtures/tool.py"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto bytes = fixture.readAll();
        for (const auto &name : QStringList {"apt-cache", "apt-get", "pkexec", "dpkg-query", "flatpak"})
        {
            const auto path = m_root->filePath("bin/" + name);
            put(path, bytes);
            QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        }
        qputenv("PATH", m_root->filePath("bin").toUtf8() + ':' + m_path);
    }
    void cleanup()
    {
        QTRY_VERIFY(!Karyo::Apps::SoftwareJob::hasActiveJobs());
        auto reaped = [] {
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
    void optionalCatalogAndCondaHaveUsefulEmptyStates()
    {
        PackageManager manager;
        QVERIFY(manager.catalog().isEmpty());
        std::unique_ptr<Page> discover(createDiscoverPage(&manager));
        QVERIFY(labels(discover.get()).contains("Find apps and tools"));
        QVERIFY(!button(discover.get(), "Install"));
        QVERIFY(labels(discover.get()).contains("Updates have not been checked"));
        std::unique_ptr<Page> envs(createEnvironmentsPage(&manager));
        envs->refresh();
        QVERIFY(labels(envs.get()).contains("Set up conda environments"));
        QVERIFY(button(envs.get(), "Set up Miniforge"));
    }
    void updatesFailureIsVisibleAndRetryRestoresActions()
    {
        PackageManager manager;
        std::unique_ptr<Page> updates(createUpdatesPage(&manager));
        auto *check = button(updates.get(), "Check for updates");
        QVERIFY(check);
        put(m_root->filePath("fail-query"), "yes");
        manager.checkUpdates();
        QVERIFY(!check->isEnabled());
        QTRY_VERIFY(!manager.updatesError().isEmpty());
        QVERIFY(labels(updates.get()).contains("Could not check for updates"));
        QVERIFY(check->isEnabled());
        QVERIFY(QFile::remove(m_root->filePath("fail-query")));
        manager.checkUpdates();
        QTRY_VERIFY(manager.updatesKnown());
        QVERIFY(check->isEnabled());
        QVERIFY(labels(updates.get()).contains("packages can be updated"));
        put(m_root->filePath("deny-job"), "yes");
        auto *all = button(updates.get(), "Update all");
        QVERIFY(all && all->isEnabled());
        QSignalSpy failed(&manager, &PackageManager::jobFailed);
        all->click();
        QVERIFY(!all->isEnabled());
        QVERIFY(!check->isEnabled());
        QTRY_COMPARE(failed.size(), 1);
        // Finishing a transaction schedules a new asynchronous update query.
        QTRY_VERIFY(check->isEnabled());
        QTRY_VERIFY(all->isEnabled());
    }
    void searchAndDetailsUsePlainTextAndTrackJobs()
    {
        PackageManager manager;
        SearchPage search(&manager);
        search.search("qa");
        QTRY_VERIFY(button(&search, "Install"));
        QVERIFY(labels(&search).contains("System packages"));
        CatalogApp app;
        app.id = "qa-new";
        app.name = "QA";
        app.source = "apt";
        app.package = "qa-new";
        app.summary = "<b>Literal catalog text</b>";
        app.fromCatalog = false;
        auto details = std::make_unique<DetailsPage>(&manager, app);
        bool plain = false;
        for (auto *label : details->findChildren<QLabel *>())
            if (label->text() == app.summary)
                plain = label->textFormat() == Qt::PlainText;
        QVERIFY(plain);
        auto *install = button(details.get(), "Install");
        QVERIFY(install && install->isEnabled());
        install->click();
        QVERIFY(manager.isBusy(app));
        QVERIFY(!install->isEnabled());
        details.reset(); // The application-owned transaction outlives this page.
        QTRY_VERIFY(!manager.isBusy(app));
        QVERIFY(QFileInfo::exists(m_root->filePath("job-completed")));
    }
};
QTEST_MAIN(PagesTest)
#include "pagestest.moc"
