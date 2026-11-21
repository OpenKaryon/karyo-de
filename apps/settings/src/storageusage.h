// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include <QList>
#include <QStringList>
#include <atomic>

namespace StorageUsage {
struct Part {
    QString key, glyph, path;
    qint64 bytes = 0;
};
struct Scan {
    QList<Part> parts;
    qint64 total = 0;
    QList<QPair<qint64, QString>> largest;
    QHash<QString, qint64> extras;
    QStringList errors;
};
QStringList condaRoots(const QString &home);
// Physical, same-filesystem walk. Categories are exclusive; extras are subsets.
// Hard links count once, sparse files by allocated blocks. Cancellation is cooperative.
Scan measure(const QString &home, QList<Part> parts, const std::atomic_bool &cancel,
             const QString &aptArchives = QStringLiteral("/var/cache/apt/archives"),
             const QString &dpkgStatus = QStringLiteral("/var/lib/dpkg/status"));
}
