// SPDX-License-Identifier: GPL-3.0-or-later
// Clipboard of files (interoperable with other file managers) and file jobs
// that copy, move or delete in a worker thread.
#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>

namespace FileClipboard {

void set(const QStringList &paths, bool cut);
QStringList paths(bool *cut = nullptr);
bool hasFiles();

} // namespace FileClipboard

class FileJob : public QObject
{
    Q_OBJECT
public:
    enum class Kind { Copy, Move, Delete };

    // Starts in its own thread; the job deletes itself when done.
    static FileJob *start(Kind kind, const QStringList &sources, const QString &targetDir = {});

    // A free compound-suffix name in dir, or empty when none can be chosen.
    static QString freeName(const QString &dir, const QString &name);

Q_SIGNALS:
    void progress(qint64 done, qint64 total, const QString &current);
    void finished(const QStringList &errors);

private:
    FileJob(Kind kind, const QStringList &sources, const QString &targetDir);
    void run();
    bool copyPath(const QString &source, const QString &target, qint64 &done, qint64 total);

    Kind m_kind;
    QStringList m_sources;
    QString m_target;
    QStringList m_errors;
};
