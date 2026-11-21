// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace {

const QStringList &audioSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral("mp3"),  QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("opus"), QStringLiteral("m4a"),  QStringLiteral("aac"), QStringLiteral("wav"),
        QStringLiteral("wma"),  QStringLiteral("aiff"), QStringLiteral("aif"), QStringLiteral("alac"),
        QStringLiteral("ape"),  QStringLiteral("mka"),
    };
    return suffixes;
}

const QStringList &videoSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("mpg"),  QStringLiteral("mpeg"),
        QStringLiteral("ogv"), QStringLiteral("wmv"), QStringLiteral("ts"),   QStringLiteral("3gp"),
    };
    return suffixes;
}

QStringList filters(const QStringList &suffixes)
{
    QStringList out;
    for (const QString &suffix : suffixes)
        out.append(QStringLiteral("*.") + suffix);
    return out;
}

} // namespace

namespace Library {

bool isAudio(const QString &path) { return audioSuffixes().contains(QFileInfo(path).suffix().toLower()); }

bool isVideo(const QString &path) { return videoSuffixes().contains(QFileInfo(path).suffix().toLower()); }

bool isMedia(const QString &path) { return isAudio(path) || isVideo(path); }

MediaItem item(const QString &path, const QString &root)
{
    const QFileInfo info(path);
    MediaItem item;
    item.path = info.absoluteFilePath();
    item.video = isVideo(path);
    item.format = info.suffix().toUpper();
    item.size = info.size();
    item.modified = info.lastModified();
    // "01 - Song", "01. Song", "1 Song" -> "Song".
    static const QRegularExpression track(QStringLiteral("^\\d{1,3}\\s*[-._)]?\\s+"));
    item.title = info.completeBaseName();
    const QString stripped = QString(item.title).remove(track);
    if (!stripped.isEmpty())
        item.title = stripped;
    item.title.replace(QLatin1Char('_'), QLatin1Char(' '));
    // Artist/Album folders under the library root.
    QDir folder = info.dir();
    QStringList parts;
    while (parts.size() < 2 && folder.absolutePath() != QDir(root).absolutePath() && !folder.isRoot()) {
        parts.prepend(folder.dirName());
        folder.cdUp();
        if (root.isEmpty())
            break;
    }
    item.subtitle = parts.join(QStringLiteral(" · "));
    return item;
}

QList<MediaItem> scan(const QString &root, bool video, int limit)
{
    QList<MediaItem> items;
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return items;
    QDirIterator it(root, filters(video ? videoSuffixes() : audioSuffixes()), QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && items.size() < limit)
        items.append(item(it.next(), root));
    if (video) {
        std::sort(items.begin(), items.end(),
                  [](const MediaItem &a, const MediaItem &b) { return a.modified > b.modified; });
    } else {
        // Qt's C-locale collator ignores numericMode. Keep natural track
        // ordering in minimal/unlocalized sessions as well.
        const QLocale locale;
        QCollator collator(locale.language() == QLocale::C
                               ? QLocale(QLocale::English, QLocale::UnitedStates) : locale);
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        // Album order: folder first, then the file name with its track number.
        std::sort(items.begin(), items.end(), [&collator](const MediaItem &a, const MediaItem &b) {
            const int folder = collator.compare(QFileInfo(a.path).path(), QFileInfo(b.path).path());
            if (folder != 0)
                return folder < 0;
            return collator.compare(QFileInfo(a.path).fileName(), QFileInfo(b.path).fileName()) < 0;
        });
    }
    return items;
}

QString duration(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("0:00");
    const qint64 seconds = ms / 1000;
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds / 60) % 60;
    const qint64 s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace Library
