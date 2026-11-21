// SPDX-License-Identifier: GPL-3.0-or-later
#include "archives.h"

#include "fileops.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "archivebackend.h"

namespace {

// Longest first, so .tar.gz wins over .gz.
const QStringList ArchiveSuffixes = {
    QStringLiteral(".tar.gz"),  QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"), QStringLiteral(".tar.zst"),
    QStringLiteral(".tgz"),     QStringLiteral(".tbz2"),    QStringLiteral(".tbz"),    QStringLiteral(".txz"),
    QStringLiteral(".tzst"),    QStringLiteral(".tar"),     QStringLiteral(".zip"),    QStringLiteral(".7z"),
    QStringLiteral(".rar"),     QStringLiteral(".cab"),
};
const QStringList CompressedSuffixes = {
    QStringLiteral(".gz"), QStringLiteral(".bz2"), QStringLiteral(".xz"), QStringLiteral(".zst"), QStringLiteral(".lz4"),
};

QString suffixOf(const QString &name, const QStringList &suffixes)
{
    const QString lower = name.toLower();
    for (const QString &suffix : suffixes) {
        if (lower.endsWith(suffix) && lower.size() > suffix.size())
            return suffix;
    }
    return {};
}

} // namespace

namespace Archives {

Kind kindOf(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    if (!suffixOf(name, ArchiveSuffixes).isEmpty())
        return Kind::Archive;
    if (!suffixOf(name, CompressedSuffixes).isEmpty())
        return Kind::Compressed;
    return Kind::None;
}

QString stem(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    QString suffix = suffixOf(name, ArchiveSuffixes);
    if (suffix.isEmpty())
        suffix = suffixOf(name, CompressedSuffixes);
    return name.left(name.size() - suffix.size());
}

bool available() { return true; } // libarchive is linked; stream helpers report missing tools.

} // namespace Archives

namespace {
using Outcome = QPair<QString, QString>;

Outcome publish(const QString &staged, const QString &parent, const QString &preferred)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        const QString name = FileJob::freeName(parent, preferred);
        if (name.isEmpty())
            break;
        const QString target = QDir(parent).filePath(name);
        const auto from = QFile::encodeName(staged), to = QFile::encodeName(target);
        if (::syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_NOREPLACE) == 0)
            return {target, {}};
        if (errno != EEXIST && errno != ENOTEMPTY)
            break;
    }
    return {{}, ArchiveJob::tr("Could not publish the archive result.")};
}

Outcome stream(const QString &program, const QStringList &arguments, const QString &parent, const QString &name)
{
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".karyo-stream-XXXXXX")));
    if (!staging.isValid())
        return {{}, ArchiveJob::tr("Cannot write to this folder.")};
    const QString executable = QStandardPaths::findExecutable(program);
    if (executable.isEmpty())
        return {{}, ArchiveJob::tr("%1 is not installed.").arg(program)};
    const QString output = staging.filePath(QStringLiteral("stream"));
    QProcess process;
    process.setStandardOutputFile(output);
    process.start(executable, arguments);
    if (!process.waitForStarted())
        return {{}, process.errorString()};
    if (!process.waitForFinished(-1) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString message = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        return {{}, message.isEmpty() ? ArchiveJob::tr("The archive tool stopped with an error.") : message};
    }
    return publish(output, parent, name);
}
} // namespace

ArchiveJob::ArchiveJob(QObject *parent) : QObject(parent) {}

void ArchiveJob::execute(std::function<QPair<QString, QString>()> operation)
{
    // Defer the start so the caller can connect finished even for invalid input.
    QTimer::singleShot(0, this, [this, operation = std::move(operation)] {
        const auto result = std::make_shared<Outcome>();
        auto *worker = QThread::create([operation, result] { *result = operation(); });
        connect(worker, &QThread::finished, this, [this, result] {
            Q_EMIT finished(result->first, result->second);
            deleteLater();
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start(QThread::LowPriority);
    });
}

ArchiveJob *ArchiveJob::extract(const QString &archive, const QString &targetDir)
{
    auto *job = new ArchiveJob;
    job->execute([archive, targetDir, job]() -> Outcome {
        QTemporaryDir staging(QDir(targetDir).filePath(QStringLiteral(".karyo-quick-extract-XXXXXX")));
        if (!staging.isValid())
            return {{}, tr("Cannot write to this folder.")};
        std::atomic_bool cancel{false};
        const auto result = ArchiveBackend::extract(archive, staging.path(), {}, {}, cancel,
            [job](qint64, qint64, const QString &name) { Q_EMIT job->progress(name); });
        if (!result.ok())
            return {{}, result.error};
        QDir folder(result.output);
        const auto entries = folder.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        if (entries.size() == 1)
            return publish(folder.filePath(entries.first()), targetDir, entries.first());
        const QString stem = Archives::stem(archive);
        return publish(result.output, targetDir, stem.isEmpty() ? QStringLiteral("Archive") : stem);
    });
    return job;
}

ArchiveJob *ArchiveJob::decompress(const QString &file)
{
    auto *job = new ArchiveJob;
    job->execute([file] {
        return stream(QStringLiteral("bsdcat"), {QStringLiteral("--"), QFileInfo(file).absoluteFilePath()},
                      QFileInfo(file).absolutePath(), Archives::stem(file));
    });
    return job;
}

ArchiveJob *ArchiveJob::compress(const QStringList &sources, Format format)
{
    auto *job = new ArchiveJob;
    job->execute([sources, format, job]() -> Outcome {
        if (sources.isEmpty())
            return {{}, tr("Choose files or folders to archive.")};
        const QFileInfo first(sources.first());
        const QString parent = first.absolutePath();
        if (format == Format::Gzip) {
            if (sources.size() != 1 || !first.isFile())
                return {{}, tr("Choose one regular file for GZIP compression.")};
            const QString program = QStandardPaths::findExecutable(QStringLiteral("pigz")).isEmpty()
                ? QStringLiteral("gzip") : QStringLiteral("pigz");
            return stream(program, {QStringLiteral("-c"), QStringLiteral("--"), first.absoluteFilePath()},
                          parent, first.fileName() + QStringLiteral(".gz"));
        }
        const QString base = first.isDir() ? first.fileName() : first.completeBaseName();
        const QString name = base + (format == Format::Zip ? QStringLiteral(".zip") : QStringLiteral(".tar.gz"));
        QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".karyo-quick-create-XXXXXX")));
        if (!staging.isValid())
            return {{}, tr("Cannot write to this folder.")};
        std::atomic_bool cancel{false};
        const auto result = ArchiveBackend::create(sources, staging.filePath(name),
            format == Format::Zip ? ArchiveBackend::Format::Zip : ArchiveBackend::Format::TarGz, cancel,
            [job](qint64, qint64, const QString &name) { Q_EMIT job->progress(name); });
        if (!result.ok())
            return {{}, result.error};
        return publish(result.output, parent, name);
    });
    return job;
}
