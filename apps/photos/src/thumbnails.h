// SPDX-License-Identifier: GPL-3.0-or-later
// Square thumbnails of pictures, decoded in worker threads at the size they
// are shown and kept in a bounded cache. The gallery grid and the filmstrip
// ask for them while they paint and repaint when ready() arrives.
#pragma once

#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QSet>

class Thumbnails : public QObject
{
    Q_OBJECT
public:
    // size: the edge in device pixels.
    Thumbnails(int size, QObject *parent = nullptr);

    // The thumbnail, or a null pixmap while it is being made.
    QPixmap get(const QString &path);

Q_SIGNALS:
    void ready(const QString &path);

private:
    int m_size;
    QCache<QString, QPixmap> m_cache;
    QSet<QString> m_pending;
};

namespace Pictures {
// File name filters for the formats Qt can read here (jpg, png, webp...).
QStringList nameFilters();
bool isPicture(const QString &path);
// The pictures in a folder, by name.
QStringList inFolder(const QString &folder);
}
