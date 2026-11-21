// SPDX-License-Identifier: GPL-3.0-or-later
#include "storageusage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <set>
#include <ftw.h>
#include <sys/stat.h>

namespace StorageUsage {
namespace {
struct Walk {
    Scan *scan;
    const std::atomic_bool *cancel;
    QList<QPair<QString, QString>> extras, conda;
    std::set<std::pair<dev_t, ino_t>> hardLinks;
};
thread_local Walk *active = nullptr;
bool within(const QString &file, const QString &root)
{
    return !root.isEmpty() && file.startsWith(QDir::cleanPath(root) + QLatin1Char('/'));
}
int visit(const char *path, const struct stat *st, int type, struct FTW *)
{
    auto &walk = *active;
    if (walk.cancel->load()) return 1;
    const QString file = QFile::decodeName(path);
    if (type == FTW_DNR || type == FTW_NS) {
        if (walk.scan->errors.size() < 10) walk.scan->errors.append(file);
        return 0;
    }
    if (type != FTW_F) return 0;
    if (st->st_nlink > 1 && !walk.hardLinks.insert({st->st_dev, st->st_ino}).second) return 0;
    const qint64 bytes = qint64(st->st_blocks) * 512;
    auto &scan = *walk.scan;
    scan.total += bytes;
    // The most specific main category owns a file, even with overlapping XDG paths.
    int match = -1, length = -1;
    for (int i = 0; i < scan.parts.size(); ++i) {
        const auto &part = scan.parts.at(i);
        if (within(file, part.path) && part.path.size() > length) { match = i; length = part.path.size(); }
        if (part.key == QLatin1String("conda"))
            for (const auto &root : walk.conda)
                if (within(file, root.second) && root.second.size() > length) { match = i; length = root.second.size(); }
    }
    if (match < 0) {
        for (int i = 0; i < scan.parts.size(); ++i)
            if (scan.parts.at(i).key == QLatin1String("other")) { match = i; break; }
    }
    if (match >= 0) scan.parts[match].bytes += bytes;
    for (const auto &extra : walk.extras)
        if (within(file, extra.second)) scan.extras[extra.first] += bytes;
    if (bytes >= 100LL * 1024 * 1024) {
        scan.largest.append({bytes, file});
        std::sort(scan.largest.begin(), scan.largest.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        if (scan.largest.size() > 10) scan.largest.removeLast();
    }
    return 0;
}
}
QStringList condaRoots(const QString &home)
{
    QStringList roots;
    for (const char *name : {"miniforge3", "mambaforge", "miniconda3", "anaconda3", ".conda"}) {
        const QString path = QDir(home).filePath(QString::fromLatin1(name));
        if (QFileInfo(path).isDir() && !QFileInfo(path).isSymLink()) roots.append(path);
    }
    return roots;
}
Scan measure(const QString &home, QList<Part> parts, const std::atomic_bool &cancel,
             const QString &aptArchives, const QString &dpkgStatus)
{
    Scan scan;
    scan.parts = parts;
    for (auto &part : scan.parts) part.bytes = 0;
    Walk walk{&scan, &cancel, {}, {}, {}};
    const QString cache = QDir(home).filePath(QStringLiteral(".cache"));
    walk.extras = {{QStringLiteral("pip"), cache + QStringLiteral("/pip")},
                   {QStringLiteral("thumbnails"), cache + QStringLiteral("/thumbnails")}};
    for (const QString &root : condaRoots(home)) {
        walk.conda.append({QStringLiteral("conda"), root});
        walk.extras.append({QStringLiteral("condapkgs"), root + QStringLiteral("/pkgs")});
    }
    if (!cancel.load()) {
        if (!QFileInfo(home).isDir() || QFileInfo(home).isSymLink()) scan.errors.append(home);
        else {
            active = &walk;
            const int result = nftw(QFile::encodeName(home).constData(), visit, 32, FTW_PHYS | FTW_MOUNT);
            if (result < 0 && !cancel.load()) scan.errors.append(home + QStringLiteral(": ") + QString::fromLocal8Bit(strerror(errno)));
            active = nullptr;
        }
    }
    if (cancel.load()) return scan;
    qint64 apt = 0;
    for (const QFileInfo &deb : QDir(aptArchives).entryInfoList({QStringLiteral("*.deb")}, QDir::Files | QDir::NoSymLinks))
        apt += deb.size();
    scan.extras.insert(QStringLiteral("apt"), apt);
    QFile status(dpkgStatus);
    qint64 apps = 0;
    if (status.open(QIODevice::ReadOnly)) {
        bool installed = false;
        qint64 size = 0;
        auto finish = [&] { if (installed) apps += size; installed = false; size = 0; };
        while (!status.atEnd()) {
            const QByteArray line = status.readLine().trimmed();
            if (line.isEmpty()) finish();
            else if (line.startsWith("Status:")) installed = line.mid(7).trimmed() == "install ok installed";
            else if (line.startsWith("Installed-Size:")) size = qMax(0LL, line.mid(15).trimmed().toLongLong()) * 1024;
        }
        finish();
    } else scan.errors.append(dpkgStatus + QStringLiteral(": ") + status.errorString());
    scan.extras.insert(QStringLiteral("apps"), apps);
    return scan;
}
}
