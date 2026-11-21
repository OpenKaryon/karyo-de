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
#include <QTemporaryDir>
#include <QTimer>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>

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
    if (cut)
        *cut = false;
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
    return data && !paths().isEmpty();
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
    // Give callers a chance to connect completion before even an empty job runs.
    QTimer::singleShot(0, thread, [thread] { thread->start(); });
    return job;
}

namespace {

bool occupied(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink(); // A dangling link still owns its name.
}

// Linux native publication must not overwrite a concurrent copy or an empty directory.
// Missing kernel support is a reported failure, never an unsafe rename fallback.
bool publish(const QString &source, const QString &target)
{
    const QByteArray from = QFile::encodeName(source), to = QFile::encodeName(target);
    return ::syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD,
                     to.constData(), RENAME_NOREPLACE) == 0;
}

} // namespace

QString FileJob::freeName(const QString &dir, const QString &name)
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QLatin1Char('/')))
        return {};
    const QDir target(dir);
    if (!occupied(target.filePath(name)))
        return name;
    // Keep compound suffixes together: reads.fastq.gz -> reads (2).fastq.gz.
    const qsizetype dot = name.indexOf(QLatin1Char('.'), 1);
    const QString base = dot > 0 ? name.left(dot) : name;
    const QString suffix = dot > 0 ? name.mid(dot) : QString();
    for (quint64 i = 2; i < 1'000'000; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(suffix);
        if (!occupied(target.filePath(candidate)))
            return candidate;
    }
    return {}; // Never return the original occupied name on exhaustion.
}

void FileJob::run()
{
    const QFileInfo destination(m_target);
    if (m_kind != Kind::Delete && (!destination.isDir() || !destination.isWritable())) {
        Q_EMIT finished({tr("The destination folder is not writable.")});
        return;
    }
    qint64 total = 0;
    if (m_kind != Kind::Delete) {
        for (const QString &source : std::as_const(m_sources)) {
            const QFileInfo info(source);
            if (info.isDir() && !info.isSymLink()) {
                QDirIterator it(source, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const auto entry = it.nextFileInfo();
                    if (!entry.isSymLink())
                        total += entry.size();
                }
            } else if (!info.isSymLink()) {
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
        const QString sourcePath = info.canonicalFilePath();
        const QString destinationPath = destination.canonicalFilePath();
        if (!occupied(source) || info.fileName().isEmpty()
            || (!info.isSymLink() && sourcePath == QLatin1String("/"))) {
            m_errors.append(tr("Could not copy %1").arg(source));
            continue;
        }
        if (info.isDir() && !info.isSymLink()
            && (destinationPath == sourcePath || destinationPath.startsWith(sourcePath + QLatin1Char('/')))) {
            m_errors.append(tr("Cannot copy %1 into itself").arg(info.fileName()));
            continue;
        }
        const bool sameDir = destinationPath == info.absoluteDir().canonicalPath();
        if (m_kind == Kind::Move && sameDir)
            continue;
        QString name = freeName(m_target, info.fileName());
        if (name.isEmpty()) {
            m_errors.append(tr("No available name for %1").arg(info.fileName()));
            continue;
        }
        QString target = QDir(m_target).filePath(name);
        if (m_kind == Kind::Move && publish(source, target)) {
            done += info.isDir() ? 0 : info.size();
            Q_EMIT progress(done, total, name);
            continue;
        }
        // Stage the complete tree on the destination filesystem. Failed copies remain
        // unpublished, and moves retain their source until publication succeeds.
        QTemporaryDir staging(QDir(m_target).filePath(QStringLiteral(".karyo-copy-XXXXXX")));
        if (!staging.isValid()) {
            m_errors.append(tr("Could not create a temporary copy in %1").arg(m_target));
            continue;
        }
        const QString payload = staging.filePath(QStringLiteral("payload"));
        if (!copyPath(source, payload, done, total))
            continue;
        bool published = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (publish(payload, target)) {
                published = true;
                break;
            }
            if (errno != EEXIST && errno != ENOTEMPTY)
                break;
            name = freeName(m_target, info.fileName());
            if (name.isEmpty())
                break;
            target = QDir(m_target).filePath(name);
        }
        if (!published) {
            m_errors.append(tr("Could not publish the copy of %1").arg(info.fileName()));
            continue;
        }
        if (m_kind == Kind::Move) {
            const bool removed = info.isDir() && !info.isSymLink() ? QDir(source).removeRecursively()
                                                                   : QFile::remove(source);
            if (!removed)
                m_errors.append(tr("Copied, but could not remove %1").arg(info.fileName()));
        }
    }
    Q_EMIT finished(m_errors);
}

bool FileJob::copyPath(const QString &source, const QString &target, qint64 &done, qint64 total)
{
    namespace fs = std::filesystem;
    const QFileInfo info(source);
    const fs::path from(QFile::encodeName(source).constData()), to(QFile::encodeName(target).constData());
    std::error_code error;
    if (info.isSymLink()) {
        const auto link = fs::read_symlink(from, error);
        if (!error)
            fs::create_symlink(link, to, error); // Preserve relative and dangling links verbatim.
        if (error)
            m_errors.append(tr("Could not copy the link %1").arg(info.fileName()));
        return !error;
    }
    if (info.isDir()) {
        fs::create_directory(to, error);
        if (error) {
            m_errors.append(tr("Could not create %1").arg(QFileInfo(target).fileName()));
            return false;
        }
        fs::directory_iterator it(from, error);
        while (!error && it != fs::directory_iterator()) {
            const QString child = QFile::decodeName(it->path().c_str());
            if (!copyPath(child, QDir(target).filePath(QFileInfo(child).fileName()), done, total))
                return false;
            it.increment(error);
        }
        if (error) {
            m_errors.append(tr("Could not read the folder %1").arg(info.fileName()));
            return false;
        }
        QFile::setPermissions(target, info.permissions());
        return true;
    }
    if (!info.isFile()) {
        m_errors.append(tr("Cannot copy the special file %1").arg(info.fileName()));
        return false;
    }
    QFile in(source), out(target);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        m_errors.append(tr("Could not copy %1").arg(info.fileName()));
        return false;
    }
    while (true) {
        const QByteArray buffer = in.read(4 * 1024 * 1024);
        if (buffer.isEmpty()) {
            if (in.error() == QFileDevice::NoError)
                break;
            m_errors.append(tr("Could not read %1").arg(info.fileName()));
            return false;
        }
        if (out.write(buffer) != buffer.size()) {
            m_errors.append(tr("Not enough space to copy %1").arg(info.fileName()));
            return false;
        }
        done += buffer.size();
        Q_EMIT progress(done, total, info.fileName());
    }
    if (!out.flush()) {
        m_errors.append(tr("Could not finish writing %1").arg(info.fileName()));
        return false;
    }
    out.setPermissions(in.permissions());
    out.setFileTime(info.lastModified(), QFileDevice::FileModificationTime);
    return true;
}
