// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QList>
#include <QStringList>

struct CatalogApp
{
    QString id, name, tile, source, package, kind, desktop, command, category, summary;
    bool fromCatalog = true;
};
struct CatalogData
{
    QList<CatalogApp> apps;
    QStringList featured;
    QString path, error;
};
// Same XDG data path as the shell. The highest-priority file is authoritative;
// an invalid personal override must not silently fall back to another catalog.
CatalogData readCatalog(const QString &path = { });
