// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace
{
bool matches(const QString &value, const QString &pattern)
{
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern)).match(value).hasMatch();
}
QString tr(const char *source) { return QCoreApplication::translate("AppCatalog", source); }
bool valid(const CatalogApp &app)
{
    if (!matches(app.id, QStringLiteral("[A-Za-z0-9][A-Za-z0-9._+-]{0,127}")) || app.name.isEmpty()
        || app.name.size() > 240 || app.summary.size() > 4096)
        return false;
    if (app.kind != QLatin1String("cli") && app.kind != QLatin1String("gui"))
        return false;
    if (app.source == QLatin1String("apt"))
    {
        if (!matches(app.package, QStringLiteral("[a-z0-9][a-z0-9+.-]{0,254}(?::[a-z0-9][a-z0-9-]{0,31})?")))
            return false;
    }
    else if (app.source == QLatin1String("flatpak"))
    {
        if (!matches(app.package, QStringLiteral("[A-Za-z0-9][A-Za-z0-9._-]{0,254}"))
            || !app.package.contains(QLatin1Char('.')))
            return false;
    }
    else if (app.source == QLatin1String("bioconda") || app.source == QLatin1String("conda-forge"))
    {
        if (!matches(app.package, QStringLiteral("[a-z0-9][a-z0-9_.-]{0,127}")))
            return false;
    }
    else
        return false;
    // Commands are executable names, never shell fragments or paths.
    if (!app.command.isEmpty() && !matches(app.command, QStringLiteral("[A-Za-z0-9][A-Za-z0-9_.+-]{0,127}")))
        return false;
    if (!app.desktop.isEmpty()
        && !matches(app.desktop, QStringLiteral("[A-Za-z0-9][A-Za-z0-9_.+-]{0,240}\\.desktop")))
        return false;
    return true;
}
}
CatalogData readCatalog(const QString &path)
{
    CatalogData result;
    result.path = path.isEmpty()
        ? QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("karyo/catalog.json"))
        : path;
    if (result.path.isEmpty())
        return result;
    QFile file(result.path);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = tr("Could not read the application catalog: %1").arg(file.errorString());
        return result;
    }
    constexpr qint64 limit = 4 * 1024 * 1024;
    if (file.size() > limit)
    {
        result.error = tr("The application catalog is too large.");
        return result;
    }
    const auto bytes = file.read(limit + 1);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || !root.value(QStringLiteral("apps")).isArray()
        || (root.contains(QStringLiteral("schema")) && root.value(QStringLiteral("schema")) != QJsonValue(1)))
    {
        result.error = tr("The application catalog has an invalid format.");
        return result;
    }
    QSet<QString> ids;
    QList<CatalogApp> apps;
    const bool russian = QLocale().language() == QLocale::Russian;
    for (const auto &entry : root.value(QStringLiteral("apps")).toArray())
    {
        const auto object = entry.toObject();
        CatalogApp app;
        app.id = object.value(QStringLiteral("id")).toString();
        app.name = object.value(QStringLiteral("name")).toString();
        app.tile = object.value(QStringLiteral("tile")).toString().left(3);
        app.source = object.value(QStringLiteral("source")).toString();
        app.package = object.value(QStringLiteral("package")).toString();
        app.kind = object.value(QStringLiteral("kind")).toString();
        app.desktop = object.value(QStringLiteral("desktop")).toString();
        app.command = object.value(QStringLiteral("command")).toString();
        app.category = object.value(QStringLiteral("category")).toString();
        app.summary = object
                          .value(russian && object.contains(QStringLiteral("summary_ru"))
                                  ? QStringLiteral("summary_ru")
                                  : QStringLiteral("summary"))
                          .toString();
        if (!valid(app) || ids.contains(app.id))
        {
            result.error
                = tr("The application catalog contains an invalid or duplicate entry: %1").arg(app.id);
            return result;
        }
        ids.insert(app.id);
        apps.append(app);
    }
    result.apps = apps;
    for (const auto &entry : root.value(QStringLiteral("featured")).toArray())
        if (ids.contains(entry.toString()) && !result.featured.contains(entry.toString()))
            result.featured.append(entry.toString());
    return result;
}
