// SPDX-License-Identifier: GPL-3.0-or-later
// What Media Player lists: the songs under Music and the videos under Videos,
// found in a worker thread. Names come from the files and their folders
// (Artist/Album/01 Song.flac); the tags are read when a file plays.
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

struct MediaItem
{
    QString path;
    QString title;     // "Song" from "01 - Song.flac"
    QString subtitle;  // "Artist · Album" from the folders
    QString format;    // FLAC, MP4
    qint64 size = 0;
    QDateTime modified;
    bool video = false;
};

namespace Library {
bool isAudio(const QString &path);
bool isVideo(const QString &path);
bool isMedia(const QString &path);
MediaItem item(const QString &path, const QString &root = QString());
// Songs or videos under root, sorted for a library (songs by folder and
// name, videos newest first). Stops after limit files.
QList<MediaItem> scan(const QString &root, bool video, int limit = 5000);
// "3:07", "1:02:45".
QString duration(qint64 ms);
}
