// SPDX-License-Identifier: GPL-3.0-or-later
// Quick archive actions use the shared backend and atomic streamed output.
#pragma once

#include <QObject>
#include <QStringList>
#include <functional>



namespace Archives {

enum class Kind { None, Archive, Compressed };
Kind kindOf(const QString &path);
// "data.tar.gz" -> "data", "reads.fastq.gz" -> "reads.fastq"
QString stem(const QString &path);
// Core archive operations are linked; stream helpers report missing dependencies.
bool available();

} // namespace Archives

class ArchiveJob : public QObject
{
    Q_OBJECT
public:
    enum class Format { Zip, TarGz, Gzip };

    // Each runs on its own and deletes itself when done.
    static ArchiveJob *extract(const QString &archive, const QString &targetDir);
    static ArchiveJob *decompress(const QString &file);
    static ArchiveJob *compress(const QStringList &sources, Format format);

Q_SIGNALS:
    void progress(const QString &current);
    // The new file or folder, or an error.
    void finished(const QString &result, const QString &error);

private:
    explicit ArchiveJob(QObject *parent = nullptr);
    void execute(std::function<QPair<QString, QString>()> operation);
};
