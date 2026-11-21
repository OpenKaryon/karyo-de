// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearancepublisher.h"
#ifdef KARYO_GREETER_INTEROP
#include "appearance.h"
#endif
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
using Karyo::Session::publishAppearance;
class AppearancePublisherTest : public QObject
{
    Q_OBJECT
    static QString login() { return QString::fromLocal8Bit(getpwuid(geteuid())->pw_name); }
    static QString file(const QTemporaryDir &root) { return root.filePath(login() + ".conf"); }
    static QByteArray read(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return { };
        return file.readAll();
    }
private Q_SLOTS:
    void publishesOnlyPublicValuesWithRealAccountIdentity()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const bool hadUser = qEnvironmentVariableIsSet("USER");
        const auto user = qgetenv("USER");
        qputenv("USER", "spoofed-other-user");
        const bool saved = publishAppearance("dark", "sage", root.path());
        if (hadUser)
            qputenv("USER", user);
        else
            qunsetenv("USER");
        QVERIFY(saved);
        QCOMPARE(read(file(root)), QByteArray("[Appearance]\ntheme=dark\naccent=sage\n"));
        QVERIFY(!QFile::exists(root.filePath("spoofed-other-user.conf")));
        struct stat info { };
        QCOMPARE(::stat(QFile::encodeName(file(root)).constData(), &info), 0);
        QCOMPARE(info.st_uid, geteuid());
        QCOMPARE(info.st_mode & 0777, mode_t(0644));
#ifdef KARYO_GREETER_INTEROP
        const auto appearance
            = readGreeterAppearance(login(), geteuid(), root.path(), root.filePath("absent"));
        QVERIFY(appearance.accepted);
        QCOMPARE(appearance.theme, QString("dark"));
        QCOMPARE(appearance.accent, QString("sage"));
#endif
    }
    void replacementIsAtomicAndDoesNotLeaveTemporaryFiles()
    {
        QTemporaryDir root;
        QVERIFY(publishAppearance("light", "terracotta", root.path()));
        QFile before(file(root));
        QVERIFY(before.open(QIODevice::ReadOnly));
        QVERIFY(publishAppearance("auto", "olive", root.path()));
        QCOMPARE(before.readAll(), QByteArray("[Appearance]\ntheme=light\naccent=terracotta\n"));
        QCOMPARE(read(file(root)), QByteArray("[Appearance]\ntheme=auto\naccent=olive\n"));
        QCOMPARE(QDir(root.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
            QStringList { login() + ".conf" });
    }
    void invalidInputPreservesPreviousPublication()
    {
        QTemporaryDir root;
        QVERIFY(publishAppearance("dark", "sage", root.path()));
        QVERIFY(!publishAppearance("dark\n[Credentials]", "sage", root.path()));
        QVERIFY(!publishAppearance("dark", "../../other", root.path()));
        QVERIFY(!publishAppearance("dark", QString(25, 'a'), root.path()));
        QVERIFY(!publishAppearance("dark", "", root.path()));
        QCOMPARE(read(file(root)), QByteArray("[Appearance]\ntheme=dark\naccent=sage\n"));
        QVERIFY(!publishAppearance("light", "sage", root.filePath("missing")));
        QVERIFY(!QFileInfo::exists(root.filePath("missing")));
    }
    void symlinksSpecialFilesAndUnsafeDirectoriesAreRejected()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto target = root.filePath("target");
        QFile targetFile(target);
        QVERIFY(targetFile.open(QIODevice::WriteOnly));
        QCOMPARE(targetFile.write("unchanged"), qint64(9));
        targetFile.close();
        QVERIFY(QFile::link(target, file(root)));
        QVERIFY(!publishAppearance("dark", "sage", root.path()));
        QCOMPARE(read(target), QByteArray("unchanged"));
        QVERIFY(QFile::remove(file(root)));
        QCOMPARE(::mkfifo(QFile::encodeName(file(root)).constData(), 0600), 0);
        QVERIFY(!publishAppearance("dark", "sage", root.path()));
        QVERIFY(QFile::remove(file(root)));
        const auto linkedDirectory = root.path() + "-link";
        QVERIFY(QFile::link(root.path(), linkedDirectory));
        QVERIFY(!publishAppearance("dark", "sage", linkedDirectory));
        QVERIFY(QFile::remove(linkedDirectory));
        QCOMPARE(::chmod(QFile::encodeName(root.path()).constData(), 0777), 0);
        QVERIFY(!publishAppearance("dark", "sage", root.path()));
        QCOMPARE(::chmod(QFile::encodeName(root.path()).constData(), 01733), 0);
        QVERIFY(publishAppearance("dark", "sage", root.path()));
        QCOMPARE(::chmod(QFile::encodeName(root.path()).constData(), 0700), 0);
    }
    void sharedDirectoryDoesNotRequireListingPermission()
    {
        QTemporaryDir temporary;
        const auto shared = qEnvironmentVariable("KARYO_TEST_PUBLICATION_DIRECTORY");
        const auto directory = shared.isEmpty() ? temporary.path() : shared;
        if (shared.isEmpty())
            QCOMPARE(::chmod(QFile::encodeName(directory).constData(), 01733), 0);
        else
            QVERIFY(!QFileInfo(directory).isReadable());
        QVERIFY(publishAppearance("dark", "sage", directory));
        const auto path = QDir(directory).filePath(login() + ".conf");
        QCOMPARE(read(path), QByteArray("[Appearance]\ntheme=dark\naccent=sage\n"));
        if (shared.isEmpty())
            QCOMPARE(::chmod(QFile::encodeName(directory).constData(), 0700), 0);
    }
    void publicationOwnershipMatchesCaller()
    {
        QTemporaryDir root;
        const auto foreign = qEnvironmentVariable("KARYO_TEST_FOREIGN_PUBLICATION_DIRECTORY");
        if (!foreign.isEmpty())
        {
            QVERIFY(!publishAppearance("dark", "olive", foreign));
            QCOMPARE(read(QDir(foreign).filePath(login() + ".conf")), QByteArray("foreign fixture"));
        }
        else
        {
            QVERIFY(publishAppearance("light", "sage", root.path()));
            QVERIFY(publishAppearance("dark", "olive", root.path()));
            struct stat info { };
            QCOMPARE(::stat(QFile::encodeName(file(root)).constData(), &info), 0);
            QCOMPARE(info.st_uid, geteuid());
        }
    }
};
QTEST_GUILESS_MAIN(AppearancePublisherTest)
#include "appearancepublishertest.moc"
