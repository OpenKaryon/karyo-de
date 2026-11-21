// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>
#include <unistd.h>
class GreeterAppearanceTest : public QObject
{
    Q_OBJECT
    static void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
private Q_SLOTS:
    void publishedAppearanceAndLegacyFallback()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto current = root.filePath("current"), legacy = root.filePath("legacy");
        put(legacy + "/owner.conf", "[Appearance]\ntheme=dark\naccent=olive\n");
        auto appearance = readGreeterAppearance("owner", geteuid(), current, legacy);
        QVERIFY(appearance.accepted);
        QCOMPARE(appearance.theme, QString("dark"));
        QCOMPARE(appearance.accent, QString("olive"));
        put(current + "/owner.conf", "[Appearance]\ntheme=\"light\"\naccent=terracotta\n");
        appearance = readGreeterAppearance("owner", geteuid(), current, legacy);
        QVERIFY(appearance.accepted);
        QCOMPARE(appearance.theme, QString("light"));
        QVERIFY(!readGreeterAppearance("owner", geteuid() + 1, current, legacy).accepted);
        QVERIFY(!readGreeterAppearance("../owner", geteuid(), current, legacy).accepted);
    }
    void invalidHigherPriorityFilesDoNotReviveOldAppearance()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto current = root.filePath("current"), legacy = root.filePath("legacy");
        put(legacy + "/owner.conf", "[Appearance]\ntheme=dark\naccent=olive\n");
        QVERIFY(QDir().mkpath(current));
        const auto path = current + "/owner.conf";
        QVERIFY(QFile::link(legacy + "/owner.conf", path));
        QVERIFY(!readGreeterAppearance("owner", geteuid(), current, legacy).accepted);
        QVERIFY(QFile::remove(path));
        QCOMPARE(::mkfifo(QFile::encodeName(path).constData(), 0600), 0);
        QVERIFY(!readGreeterAppearance("owner", geteuid(), current, legacy).accepted);
        QVERIFY(QFile::remove(path));
        put(path, QByteArray(4097, 'x'));
        QVERIFY(!readGreeterAppearance("owner", geteuid(), current, legacy).accepted);
    }
    void onlyValidatedFieldsReachOwnSettings()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto current = root.filePath("current"), legacy = root.filePath("legacy");
        put(current + "/owner.conf", "[Appearance]\ntheme=@Variant(evil)\naccent=../../other\n");
        const auto appearance = readGreeterAppearance("owner", geteuid(), current, legacy);
        QVERIFY(appearance.accepted);
        QVERIFY(appearance.theme.isEmpty());
        QVERIFY(appearance.accent.isEmpty());
        const auto own = root.filePath("own.ini");
        put(own, "[Appearance]\ntheme=dark\naccent=olive\n[Other]\nkeep=yes\n");
        QVERIFY(applyGreeterAppearance(appearance, own));
        QSettings result(own, QSettings::IniFormat);
        QVERIFY(!result.contains("Appearance/theme"));
        QVERIFY(!result.contains("Appearance/accent"));
        QCOMPARE(result.value("Other/keep").toString(), QString("yes"));
        QVERIFY(applyGreeterAppearance({ "dark", "olive", true }, own));
        result.sync();
        QCOMPARE(result.value("Appearance/theme").toString(), QString("dark"));
        QCOMPARE(result.value("Appearance/accent").toString(), QString("olive"));
    }
};
QTEST_GUILESS_MAIN(GreeterAppearanceTest)
#include "greeterappearancetest.moc"
