// SPDX-License-Identifier: GPL-3.0-or-later
#include "storageusage.h"
#include "pages.h"
#include "widgets.h"
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>

class StorageTest : public QObject {
    Q_OBJECT
    static void put(const QString &path, const QByteArray &data) {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(data), data.size());
    }
    static qint64 allocated(const QString &path) {
        struct stat st{}; if (::stat(QFile::encodeName(path).constData(), &st)) return -1;
        return qint64(st.st_blocks) * 512;
    }
private Q_SLOTS:
    void physicalWalkCategoriesAndPackageStanzas() {
        QTemporaryDir root, external; QVERIFY(root.isValid()); QVERIFY(external.isValid());
        const QString home = root.filePath("home");
        put(home + "/Documents/nested/one", QByteArray(8200, 'a'));
        put(home + "/Documents/two", QByteArray(4200, 'b'));
        put(home + "/.cache/pip/wheel", QByteArray(1200, 'c'));
        put(external.filePath("outside"), QByteArray(30000, 'x'));
        QVERIFY(QFile::link(external.path(), home + "/outside-link"));
        QVERIFY(::link(QFile::encodeName(home + "/Documents/two").constData(), QFile::encodeName(home + "/hard-link").constData()) == 0);
        QFile sparse(home + "/sparse"); QVERIFY(sparse.open(QIODevice::WriteOnly)); QVERIFY(sparse.resize(512LL*1024*1024)); sparse.close();
        const QString status = root.filePath("status"), apt = root.filePath("apt");
        put(apt + "/one.deb", "12345");
        put(status, "Package: old\nStatus: deinstall ok config-files\nInstalled-Size: 90\n\nPackage: first\nInstalled-Size: 12\nStatus: install ok installed\n\nPackage: second\nStatus: install ok installed\nInstalled-Size: 3\n");
        const QList<StorageUsage::Part> parts{{"documents", "", home + "/Documents"}, {"nested", "", home + "/Documents/nested"}, {"cache", "", home + "/.cache"}, {"other", "", ""}};
        std::atomic_bool cancel{false};
        auto scan = StorageUsage::measure(home, parts, cancel, apt, status);
        const qint64 expected = allocated(home+"/Documents/nested/one") + allocated(home+"/Documents/two") + allocated(home+"/.cache/pip/wheel") + allocated(home+"/sparse");
        QCOMPARE(scan.total, expected); QCOMPARE(scan.parts[1].bytes, allocated(home+"/Documents/nested/one"));
        qint64 sum=0; for(const auto &part: scan.parts) sum+=part.bytes; QCOMPARE(sum, scan.total);
        QCOMPARE(scan.extras["pip"], allocated(home+"/.cache/pip/wheel"));
        QCOMPARE(scan.extras["apps"], 15*1024); QCOMPARE(scan.extras["apt"], 5); QVERIFY(scan.errors.isEmpty());
        QVERIFY(scan.largest.isEmpty()); // Sparse apparent length does not consume 512 MiB.
        cancel.store(true); auto stopped = StorageUsage::measure(home, parts, cancel, apt, status); QCOMPARE(stopped.total, 0);
    }
    void quotedCondaPathAndPageLifetime() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const QByteArray oldHome=qgetenv("HOME"), oldPath=qgetenv("PATH"), oldShell=qgetenv("SHELL");
        const QString home=root.filePath("home' $(touch INJECTED)");
        const QString marker=root.filePath("arguments"), tool=home+"/miniforge3/bin/conda";
        put(home+"/miniforge3/pkgs/cache", QByteArray(1100000, 'x'));
        put(tool, "#!/bin/sh\nprintf '%s\n' \"$@\" > \"$QA_ARGUMENTS\"\n");
        QVERIFY(QFile::setPermissions(tool, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const QString terminal=root.filePath("bin/karyo-terminal");
        put(terminal, "#!/bin/sh\nshift\nexec \"$@\"\n");
        QVERIFY(QFile::setPermissions(terminal, QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        qputenv("HOME", home.toUtf8()); qputenv("SHELL", "/bin/true");
        qputenv("PATH", root.filePath("bin").toUtf8()+':'+oldPath); qputenv("QA_ARGUMENTS", marker.toUtf8());
        std::unique_ptr<SettingsPage> page(createStoragePage(nullptr));
        QAbstractButton *clean=nullptr;
        auto findClean=[&] {
            for(auto *row: page->findChildren<SettingRow *>())
                if(row->property("settingTitle").toString()=="Conda package cache") clean=row->findChild<QAbstractButton *>();
            return clean != nullptr;
        };
        QTRY_VERIFY(findClean()); clean->click(); QTRY_VERIFY(QFileInfo::exists(marker));
        QFile output(marker); QVERIFY(output.open(QIODevice::ReadOnly)); QCOMPARE(output.readAll(), QByteArray("clean\n--all\n--yes\n"));
        QVERIFY(!QFileInfo::exists(QDir::current().filePath("INJECTED")));
        page.reset();
        for(int i=0; i<10; ++i) { std::unique_ptr<SettingsPage> transient(createStoragePage(nullptr)); }
        QTest::qWait(80); // Queued worker completions must not call deleted pages.
        qputenv("HOME", oldHome); qputenv("PATH", oldPath); qputenv("SHELL", oldShell); qunsetenv("QA_ARGUMENTS");
    }
    void absentAndRedirectedRootsAreReported() {
        QTemporaryDir root; std::atomic_bool cancel{false};
        auto scan = StorageUsage::measure(root.filePath("missing"), {}, cancel, root.path(), root.filePath("no-status"));
        QVERIFY(scan.errors.size() >= 2);
        QVERIFY(QFile::link(root.path(), root.filePath("redirect")));
        auto redirected = StorageUsage::measure(root.filePath("redirect"), {}, cancel, root.path(), root.filePath("no-status"));
        QVERIFY(!redirected.errors.isEmpty()); QCOMPARE(redirected.total, 0);
    }
};
QTEST_MAIN(StorageTest)
#include "storagetest.moc"
