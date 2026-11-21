// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class CatalogTest : public QObject
{
    Q_OBJECT
    QJsonObject app() const
    {
        return { { "id", "qa-app" }, { "name", "QA App" }, { "tile", "QA" }, { "source", "apt" },
            { "package", "qa-app" }, { "kind", "cli" }, { "command", "qa-app" },
            { "summary", "Test application" } };
    }
    static void put(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
    static QByteArray document(const QJsonArray &apps)
    {
        return QJsonDocument(QJsonObject { { "schema", 1 }, { "apps", apps },
                                 { "featured", QJsonArray { "qa-app", "missing", "qa-app" } } })
            .toJson();
    }
private Q_SLOTS:
    void externalCatalogAndAuthoritativeOverride()
    {
        QTemporaryDir root;
        const auto personal = root.filePath("personal"), system = root.filePath("system");
        qputenv("XDG_DATA_HOME", personal.toUtf8());
        qputenv("XDG_DATA_DIRS", system.toUtf8());
        put(system + "/karyo/catalog.json", document({ app() }));
        auto catalog = readCatalog();
        QCOMPARE(catalog.apps.size(), 1);
        QCOMPARE(catalog.featured, QStringList { "qa-app" });
        QVERIFY(catalog.error.isEmpty());
        put(personal + "/karyo/catalog.json", "broken");
        auto invalid = readCatalog();
        QVERIFY(invalid.apps.isEmpty());
        QVERIFY(!invalid.error.isEmpty());
        QCOMPARE(invalid.path, personal + "/karyo/catalog.json");
    }
    void unsafeExecutablePackageAndDuplicateIdsFailClosed()
    {
        QTemporaryDir root;
        const auto path = root.filePath("catalog.json");
        for (const auto &field : QStringList { "command", "package", "desktop", "source", "kind" })
        {
            auto invalid = app();
            invalid[field] = "bad; touch marker";
            put(path, document({ app(), invalid }));
            const auto catalog = readCatalog(path);
            QVERIFY(!catalog.error.isEmpty());
            QVERIFY(catalog.apps.isEmpty());
        }
        put(path, document({ app(), app() }));
        QVERIFY(!readCatalog(path).error.isEmpty());
        auto valid = app();
        valid["command"] = "STAR";
        valid["desktop"] = "org.example.QA.desktop";
        valid["package"] = "libqa1:amd64";
        put(path, document({ valid }));
        QVERIFY(readCatalog(path).error.isEmpty());
    }
    void optionalCatalogAndBoundedInput()
    {
        QTemporaryDir root;
        qputenv("XDG_DATA_HOME", root.filePath("user").toUtf8());
        qputenv("XDG_DATA_DIRS", root.filePath("system").toUtf8());
        QVERIFY(readCatalog().apps.isEmpty());
        QVERIFY(readCatalog().error.isEmpty());
        const auto path = root.filePath("large");
        put(path, QByteArray(4 * 1024 * 1024 + 1, ' '));
        QVERIFY(!readCatalog(path).error.isEmpty());
        put(path, "{\"schema\":2,\"apps\":[]}");
        QVERIFY(!readCatalog(path).error.isEmpty());
    }
    void reviewedPublicCatalogRemainsCompatible()
    {
        const auto catalog = readCatalog(QFINDTESTDATA("../data/catalog.json"));
        QVERIFY2(catalog.error.isEmpty(), qPrintable(catalog.error));
        QCOMPARE(catalog.apps.size(), 58);
    }
};
QTEST_GUILESS_MAIN(CatalogTest)
#include "catalogtest.moc"
