// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDateTime>
#include <QList>
#include <QStringList>
#include <atomic>
#include <functional>

namespace ArchiveBackend {
struct Entry {
    QString path;
    qint64 size = 0;
    QDateTime modified;
    bool directory = false;
    bool link = false;
    bool encrypted = false;
};
struct Result {
    QList<Entry> entries;
    QString output;
    QString error;
    bool cancelled = false;
    bool passwordRequired = false;
    bool passwordRejected = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};
enum class Format { Zip, TarGz, SevenZip };
using Progress = std::function<void(qint64, qint64, const QString &)>;
Result list(const QString &archive, const QString &password, std::atomic_bool &cancel);
// Creates a new folder in parent. Existing files are never overwritten.
Result extract(const QString &archive, const QString &parent, const QStringList &selected,
               const QString &password, std::atomic_bool &cancel, const Progress &progress = {});
// Writes atomically; a failed or canceled operation preserves any existing output.
Result create(const QStringList &sources, const QString &output, Format format,
              std::atomic_bool &cancel, const Progress &progress = {},
              const QString &password = {}, bool encryptNames = true, int compressionLevel = -1);
QString stem(const QString &path);
}
