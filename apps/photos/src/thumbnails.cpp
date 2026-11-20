// SPDX-License-Identifier: GPL-3.0-or-later
#include "thumbnails.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>

Thumbnails::Thumbnails(int size, QObject *parent)
    : QObject(parent)
    , m_size(size)
    , m_cache(48 * 1024)  // KB
{
}

QPixmap Thumbnails::get(const QString &path)
{
    if (QPixmap *cached = m_cache.object(path))
        return *cached;
    if (m_pending.contains(path))
        return {};
    m_pending.insert(path);
    QPointer<Thumbnails> self(this);
    const int edge = m_size;
    QThreadPool::globalInstance()->start([self, path, edge] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        // Decoding at the shown size keeps a camera JPEG cheap.
        QSize size = reader.size();
        if (size.isValid()) {
            size.scale(edge, edge, Qt::KeepAspectRatioByExpanding);
            reader.setScaledSize(size);
        }
        QImage image = reader.read();
        // A square tile: covered and cropped to the middle.
        if (!image.isNull() && image.size() != QSize(edge, edge)) {
            image = image.scaled(edge, edge, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            image = image.copy((image.width() - edge) / 2, (image.height() - edge) / 2, edge, edge);
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, path, image] {
                if (!self)
                    return;
                self->m_pending.remove(path);
                auto *pixmap = new QPixmap(QPixmap::fromImage(image));
                self->m_cache.insert(path, pixmap, qMax<qsizetype>(1, image.sizeInBytes() / 1024));
                Q_EMIT self->ready(path);
            },
            Qt::QueuedConnection);
    });
    return {};
}

namespace Pictures {

QStringList nameFilters()
{
    static QStringList filters;
    if (filters.isEmpty()) {
        for (const QByteArray &format : QImageReader::supportedImageFormats()) {
            // Formats that are not pictures to look at.
            if (format == "pdf" || format == "ico" || format == "cur" || format == "icns" || format == "pbm"
                || format == "pgm" || format == "ppm" || format == "xbm" || format == "xpm")
                continue;
            filters.append(QStringLiteral("*.") + QString::fromLatin1(format));
        }
        filters.append(QStringLiteral("*.jpeg"));
        filters.removeDuplicates();
    }
    return filters;
}

bool isPicture(const QString &path)
{
    const QString suffix = QStringLiteral("*.") + QFileInfo(path).suffix().toLower();
    return nameFilters().contains(suffix);
}

QStringList inFolder(const QString &folder)
{
    QStringList names = QDir(folder).entryList(nameFilters(), QDir::Files | QDir::Readable);
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(names.begin(), names.end(), [&collator](const QString &a, const QString &b) {
        return collator.compare(a, b) < 0;
    });
    QStringList paths;
    for (const QString &name : std::as_const(names))
        paths.append(QDir(folder).filePath(name));
    return paths;
}

} // namespace Pictures
