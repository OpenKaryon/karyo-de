// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileops.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QThread>

namespace FileClipboard {

void set(const QStringList &paths, bool cut)
{
    auto *data = new QMimeData;
    QList<QUrl> urls;
    QByteArray gnome = cut ? "cut" : "copy";
    for (const QString &path : paths) {
        const QUrl url = QUrl::fromLocalFile(path);
        urls.append(url);
        gnome += '\n' + url.toEncoded();
    }
    data->setUrls(urls);
    data->setText(paths.join(QLatin1Char('\n')));
    // Nautilus, Nemo and PCManFM read the cut flag from this format; Dolphin from the KDE one.
    data->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    data->setData(QStringLiteral("application/x-kde-cutselection"), cut ? "1" : "0");
    QApplication::clipboard()->setMimeData(data);
}

QStringList paths(bool *cut)
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    QStringList result;
    if (!data)
        return result;
    bool isCut = false;
    if (data->hasFormat(QStringLiteral("x-special/gnome-copied-files")))
        isCut = data->data(QStringLiteral("x-special/gnome-copied-files")).startsWith("cut");
    else if (data->hasFormat(QStringLiteral("application/x-kde-cutselection")))
        isCut = data->data(QStringLiteral("application/x-kde-cutselection")) == "1";
    for (const QUrl &url : data->urls()) {
        if (url.isLocalFile())
            result.append(url.toLocalFile());
    }
    if (cut)
        *cut = isCut;
    return result;
}

bool hasFiles()
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    return data && data->hasUrls();
}

} // namespace FileClipboard

FileJob::FileJob(Kind kind, const QStringList &sources, const QString &targetDir)
    : m_kind(kind)
    , m_sources(sources)
    , m_target(targetDir)
{
}

FileJob *FileJob::start(Kind kind, const QStringList &sources, const QString &targetDir)
{
    auto *job = new FileJob(kind, sources, targetDir);
    auto *thread = new QThread;
    job->moveToThread(thread);
    connect(thread, &QThread::started, job, &FileJob::run);
    connect(job, &FileJob::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, job, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return job;
}

QString FileJob::freeName(const QString &dir, const QString &name)
{
    const QDir target(dir);
    if (!target.exists(name))
        return name;
    // Keep compound suffixes together: reads.fastq.gz -> reads (2).fastq.gz.
    const qsizetype dot = name.indexOf(QLatin1Char('.'), 1);
    const QString base = dot > 0 ? name.left(dot) : name;
    const QString suffix = dot > 0 ? name.mid(dot) : QString();
    for (int i = 2; i < 10000; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix);
        if (!target.exists(candidate))
            return candidate;
    }
    return name;
}

void FileJob::run()
{
    qint64 total = 0;
    if (m_kind != Kind::Delete) {
        for (const QString &source : std::as_const(m_sources)) {
            const QFileInfo info(source);
            if (info.isDir() && !info.isSymLink()) {
                QDirIterator it(source, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
                while (it.hasNext())
                    total += it.nextFileInfo().size();
            } else {
                total += info.size();
            }
        }
    }
    qint64 done = 0;
    for (const QString &source : std::as_const(m_sources)) {
        const QFileInfo info(source);
        if (m_kind == Kind::Delete) {
            Q_EMIT progress(done, qint64(m_sources.size()), info.fileName());
            const bool ok = info.isDir() && !info.isSymLink() ? QDir(source).removeRecursively()
                                                              : QFile::remove(source);
            if (!ok)
                m_errors.append(tr("Could not delete %1").arg(info.fileName()));
            ++done;
            continue;
        }
        QString name = info.fileName();
        const bool sameDir = QFileInfo(m_target).canonicalFilePath() == info.absoluteDir().canonicalPath();
        if (m_kind == Kind::Move && sameDir)
            continue;
        name = freeName(m_target, name);
        const QString target = QDir(m_target).filePath(name);
        if (m_kind == Kind::Move && QFile::rename(source, target)) {
            done += info.isDir() ? 0 : info.size();
            Q_EMIT progress(done, total, name);
            continue;
        }
        if (info.isDir() && QDir(target).absolutePath().startsWith(info.absoluteFilePath() + QLatin1Char('/'))) {
            m_errors.append(tr("Cannot copy %1 into itself").arg(name));
            continue;
        }
        if (!copyPath(source, target, done, total))
            continue;
        if (m_kind == Kind::Move) {
            const bool removed = info.isDir() && !info.isSymLink() ? QDir(source).removeRecursively()
                                                                   : QFile::remove(source);
            if (!removed)
                m_errors.append(tr("Copied, but could not remove %1").arg(name));
        }
    }
    Q_EMIT finished(m_errors);
}

bool FileJob::copyPath(const QString &source, const QString &target, qint64 &done, qint64 total)
{
    const QFileInfo info(source);
    if (info.isSymLink()) {
        if (!QFile::link(info.symLinkTarget(), target)) {
            m_errors.append(tr("Could not copy the link %1").arg(info.fileName()));
            return false;
        }
        return true;
    }
    if (info.isDir()) {
        if (!QDir().mkpath(target)) {
            m_errors.append(tr("Could not create %1").arg(QFileInfo(target).fileName()));
            return false;
        }
        bool ok = true;
        const QFileInfoList entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries)
            ok = copyPath(entry.absoluteFilePath(), QDir(target).filePath(entry.fileName()), done, total) && ok;
        return ok;
    }
    QFile in(source);
    QFile out(target);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
        m_errors.append(tr("Could not copy %1").arg(info.fileName()));
        return false;
    }
    QByteArray buffer;
    while (!in.atEnd()) {
        buffer = in.read(4 * 1024 * 1024);
        if (out.write(buffer) != buffer.size()) {
            m_errors.append(tr("Not enough space to copy %1").arg(info.fileName()));
            out.remove();
            return false;
        }
        done += buffer.size();
        Q_EMIT progress(done, total, info.fileName());
    }
    out.setPermissions(in.permissions());
    out.close();
    out.setFileTime(info.lastModified(), QFileDevice::FileModificationTime);
    return true;
}
