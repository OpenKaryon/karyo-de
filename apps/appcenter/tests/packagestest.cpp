// SPDX-License-Identifier: GPL-3.0-or-later
#include "packages.h"
#include "softwarejob.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class PackagesTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_path = qgetenv("PATH"), m_home = qgetenv("HOME");
    QString m_userHome;
    static void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    static void executable(const QString &path, const QByteArray &data)
    {
        put(path, data);
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }
    QList<QStringList> calls() const
    {
        QFile file(m_root->filePath("calls"));
        if (!file.open(QIODevice::ReadOnly))
            return { };
        QList<QStringList> result;
        for (const auto &line : file.readAll().split('\n'))
            if (!line.isEmpty())
                result.append(QJsonDocument::fromJson(line).toVariant().toStringList());
        return result;
    }
    static CatalogApp named(const QString &package, const QString &source = "apt")
    {
        CatalogApp app;
        app.id = package;
        app.name = package;
        app.source = source;
        app.package = package;
        app.kind = "cli";
        return app;
    }
private Q_SLOTS:
    void initTestCase() { QCoreApplication::setQuitLockEnabled(false); }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        m_userHome = m_root->filePath("home' $(touch INJECTED)");
        QVERIFY(QDir().mkpath(m_userHome));
        qputenv("HOME", m_userHome.toUtf8());
        qputenv("QA_SOFTWARE", m_root->path().toUtf8());
        qputenv("XDG_DATA_HOME", m_root->filePath("data").toUtf8());
        qputenv("XDG_DATA_DIRS", m_root->filePath("system").toUtf8());
        qputenv("XDG_CACHE_HOME", m_root->filePath("cache").toUtf8());
        qputenv("PATH", m_root->filePath("bin").toUtf8() + ':' + m_path);
        QFile fixture(QFINDTESTDATA("fixtures/tool.py"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto tool = fixture.readAll();
        put(m_root->filePath("tool.py"), tool);
        for (const auto &program : QStringList {
                 "dpkg-query", "apt-cache", "apt-get", "pkexec", "flatpak", "du", "karyo-terminal", "curl" })
            executable(m_root->filePath("bin/" + program), tool);
        const QJsonArray apps { QJsonObject { { "id", "installed" }, { "name", "Installed App" },
                                    { "source", "apt" }, { "package", "qa-installed" }, { "kind", "cli" } },
            QJsonObject { { "id", "new" }, { "name", "New App" }, { "source", "apt" },
                { "package", "qa-new" }, { "kind", "cli" } },
            QJsonObject { { "id", "flatpak" }, { "name", "QA Flatpak" }, { "source", "flatpak" },
                { "package", "org.example.QA" }, { "kind", "gui" } } };
        put(m_root->filePath("data/karyo/catalog.json"),
            QJsonDocument(QJsonObject { { "apps", apps } }).toJson());
        executable(m_userHome + "/miniforge3/bin/conda", tool);
        put(m_userHome + "/miniforge3/etc/profile.d/conda.sh",
            "conda() { printf '%s\\n' \"$@\" > \"$QA_SOFTWARE/activated\"; }\n");
        put(m_userHome + "/miniforge3/conda-meta/python-3.12.8-qa.json",
            QJsonDocument(QJsonObject { { "name", "python" }, { "version", "3.12.8" },
                              { "channel", "https://conda.anaconda.org/conda-forge/linux-64" },
                              { "size", 120 }, { "requested_spec", "python" } })
                .toJson());
        put(m_root->filePath("installer-script"),
            "#!/bin/sh\nprintf done > \"$QA_SOFTWARE/bootstrap-ran\"\n");
    }
    void cleanup()
    {
        QTRY_VERIFY(!Karyo::Apps::SoftwareJob::hasActiveJobs());
        auto reaped = []
        {
            for (auto *p : QCoreApplication::instance()->findChildren<QProcess *>())
                if (p->state() != QProcess::NotRunning)
                    return false;
            return true;
        };
        QTRY_VERIFY(reaped());
        qputenv("PATH", m_path);
        qputenv("HOME", m_home);
        qunsetenv("QA_SOFTWARE");
    }
    void installedCandidatesUserFlatpaksAndRefresh()
    {
        PackageManager manager;
        QVERIFY(manager.catalogError().isEmpty());
        QCOMPARE(manager.catalog().size(), 3);
        QSignalSpy changed(&manager, &PackageManager::changed);
        manager.refresh();
        QTRY_VERIFY(changed.size() >= 3);
        auto installed = named("qa-installed");
        QVERIFY(manager.isInstalled(installed));
        QCOMPARE(manager.version(installed), QString("1.0"));
        QVERIFY(manager.isAvailable(named("qa-new")));
        QVERIFY(manager.isInstalled(named("org.example.QA", "flatpak")));
        put(m_root->filePath("no-candidate"), "yes");
        changed.clear();
        manager.refresh();
        QTRY_VERIFY(changed.size() >= 3);
        QVERIFY(!manager.isAvailable(named("qa-new")));
        CatalogApp found = named("qa-found");
        found.fromCatalog = false;
        QVERIFY(manager.isAvailable(found));
    }
    void queryFailurePreservesKnownStateAndSearchIsLiteral()
    {
        PackageManager manager;
        QSignalSpy changed(&manager, &PackageManager::changed);
        manager.refresh();
        QTRY_VERIFY(changed.size() >= 3);
        put(m_root->filePath("fail-query"), "yes");
        QSignalSpy failed(&manager, &PackageManager::queryFailed);
        manager.refresh();
        QTRY_VERIFY(!failed.isEmpty());
        QVERIFY(manager.isInstalled(named("qa-installed")));
        QVERIFY(QFile::remove(m_root->filePath("fail-query")));
        QSignalSpy results(&manager, &PackageManager::aptResults);
        manager.searchApt("--option-looking");
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(calls().contains(QStringList({ "apt-cache", "search", "--", "--option-looking" })));
        QSignalSpy updates(&manager, &PackageManager::updatesChanged);
        manager.checkUpdates();
        QTRY_COMPARE(updates.size(), 2);
        QCOMPARE(manager.updates().size(), 1);
        QCOMPARE(manager.updates().first().to, QString("1.0"));
    }
    void aptJobsAreSerializedAndFailuresReleaseBusyState()
    {
        PackageManager manager;
        const auto app = named("qa-new");
        QSignalSpy changed(&manager, &PackageManager::jobChanged),
            failed(&manager, &PackageManager::jobFailed);
        put(m_root->filePath("deny-job"), "yes");
        manager.install(app);
        QVERIFY(manager.isBusy(app));
        manager.install(app);
        manager.remove(named("qa-other"));
        QTRY_VERIFY(!manager.isBusy(app));
        QVERIFY(failed.size() >= 2);
        int installs = 0;
        for (const auto &call : calls())
            if (call.first() == "pkexec" && call.contains("install"))
            {
                ++installs;
                QVERIFY(call.at(1).endsWith("/karyo/appcenter-apt"));
                QCOMPARE(call.last(), QString("qa-new"));
            }
        QCOMPARE(installs, 1);
        QVERIFY(QFile::remove(m_root->filePath("deny-job")));
        manager.install(app);
        QTRY_VERIFY(!manager.isBusy(app));
        QVERIFY(QFileInfo::exists(m_root->filePath("job-completed")));
        auto countPrivileged = [this]
        {
            int count = 0;
            for (const auto &call : calls())
                if (call.first() == "pkexec")
                    ++count;
            return count;
        };
        const auto before = countPrivileged();
        auto invalid = named("--unsafe");
        manager.install(invalid);
        QVERIFY(!manager.isBusy(invalid));
        QVERIFY(failed.last().at(1).toString().contains("Invalid"));
        manager.install(named("qa-new", "unknown-source"));
        QTest::qWait(80);
        QCOMPARE(countPrivileged(), before);
    }
    void flatpakAndCondaUseUserScopeAndLiteralPrefix()
    {
        PackageManager manager;
        auto flatpak = named("org.example.New", "flatpak");
        manager.install(flatpak);
        QTRY_VERIFY(!manager.isBusy(flatpak));
        bool scoped = false;
        for (const auto &call : calls())
            if (call.first() == "flatpak" && call.contains("install"))
                scoped = call.contains("--user") && call.last() == flatpak.package;
        QVERIFY(scoped);
        auto conda = named("qa-tool", "bioconda");
        const QString prefix = m_userHome + "/envs/test env";
        manager.install(conda, prefix);
        QTRY_VERIFY(!manager.isBusy(conda));
        QVERIFY(calls().contains(QStringList(
            { "conda", "install", "-y", "-p", prefix, "-c", "bioconda", "-c", "conda-forge", "qa-tool" })));
        const auto envs = manager.condaEnvs();
        QVERIFY(!envs.isEmpty());
        QCOMPARE(envs.first().python, QString("Python 3.12"));
        QCOMPARE(manager.condaPackages(manager.condaRoot()).first().channel, QString("conda-forge"));
        QSignalSpy measured(&manager, &PackageManager::envSizeKnown);
        manager.measureEnv(prefix);
        QTRY_COMPARE(measured.size(), 1);
        QCOMPARE(measured.first().at(1).toLongLong(), 12345);
    }
    void environmentTerminalQuotesPathsAndReportsPublicationFailure()
    {
        PackageManager manager;
        const QString prefix = m_userHome + "/env' $(touch INJECTED)";
        manager.openEnvTerminal(prefix);
        QTRY_VERIFY(QFileInfo::exists(m_root->filePath("activated")));
        QFile activated(m_root->filePath("activated"));
        QVERIFY(activated.open(QIODevice::ReadOnly));
        QCOMPARE(activated.readAll(), QString("activate\n%1\n").arg(prefix).toUtf8());
        QVERIFY(!QFileInfo::exists("/tmp/INJECTED"));
        put(m_root->filePath("blocked"), "file");
        qputenv("XDG_CACHE_HOME", m_root->filePath("blocked").toUtf8());
        QSignalSpy failed(&manager, &PackageManager::queryFailed);
        manager.openEnvTerminal(prefix);
        QVERIFY(!failed.isEmpty());
    }
    void failedUpdateReadKeepsPreviousState()
    {
        PackageManager manager;
        QVERIFY(!manager.updatesKnown());
        manager.checkUpdates();
        QVERIFY(manager.checkingUpdates());
        QTRY_VERIFY(manager.updatesKnown());
        QCOMPARE(manager.updates().size(), 1);
        put(m_root->filePath("fail-query"), "yes");
        QSignalSpy failed(&manager, &PackageManager::queryFailed);
        manager.checkUpdates();
        QTRY_VERIFY(!manager.checkingUpdates());
        QCOMPARE(manager.updates().size(), 1);
        QVERIFY(!manager.updatesError().isEmpty());
        QCOMPARE(failed.size(), 1);
    }
    void environmentExportIsAtomicAndArgumentsStayLiteral()
    {
        PackageManager manager;
        const QString prefix = m_userHome + "/env' $(touch INJECTED)";
        const QString target = m_userHome + "/export.yml";
        put(target, "previous export");
        QSignalSpy failed(&manager, &PackageManager::queryFailed);
        QSignalSpy saved(&manager, &PackageManager::environmentExported);
        put(m_root->filePath("deny-job"), "yes");
        manager.exportEnvironment(prefix, target);
        QTRY_COMPARE(failed.size(), 1);
        QFile old(target);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), QByteArray("previous export"));
        old.close();
        QVERIFY(QFile::remove(m_root->filePath("deny-job")));
        manager.exportEnvironment(prefix, target);
        QTRY_COMPARE(saved.size(), 1);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), QByteArray("name: qa-env\ndependencies: [python]\n"));
        QVERIFY(calls().contains(QStringList({"conda", "env", "export", "-p", prefix})));
        manager.exportEnvironment(prefix, m_userHome + "/missing/target.yml");
        QTRY_COMPARE(failed.size(), 2);
        const QString input = m_userHome + "/a' $(touch INJECTED).yml";
        put(input, "name: qa-env");
        manager.importEnvironment(input);
        QTRY_VERIFY(calls().contains(QStringList({"conda", "env", "create", "-f", input})));
        const auto before = calls().size();
        manager.createEnvironment("--unsafe");
        QCOMPARE(failed.size(), 3);
        QCOMPARE(calls().size(), before);
        manager.createEnvironment("qa-project_1");
        QTRY_VERIFY(calls().contains(QStringList({"conda", "create", "-y", "-n", "qa-project_1", "python=3.12"})));
        auto terminalCount = [this] {
            QFile file(m_root->filePath("terminal-finished"));
            return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
        };
        QTRY_COMPARE(terminalCount(), 2);
        QVERIFY(!QFileInfo::exists(m_userHome + "/INJECTED"));
    }
    void bootstrapChecksChecksumBeforeUserInstaller()
    {
        PackageManager manager;
        QProcess process;
        put(m_root->filePath("bad-sha"), "yes");
        process.start("/bin/sh", { "-c", manager.installMiniforgeCommand() });
        QVERIFY(process.waitForFinished(5000));
        QVERIFY(process.exitCode() != 0);
        QVERIFY(!QFileInfo::exists(m_root->filePath("bootstrap-ran")));
        QVERIFY(QFile::remove(m_root->filePath("bad-sha")));
        process.start("/bin/sh", { "-c", manager.installMiniforgeCommand() });
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(QFileInfo::exists(m_root->filePath("bootstrap-ran")));
        for (const auto &call : calls())
            if (call.first() == "curl" && call.last().endsWith(".sha256"))
                QVERIFY(call.last().contains("/26.9.4-2/Miniforge3-26.9.4-2-Linux-"));
    }
};
QTEST_GUILESS_MAIN(PackagesTest)
#include "packagestest.moc"
