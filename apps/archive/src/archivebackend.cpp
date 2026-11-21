// SPDX-License-Identifier: GPL-3.0-or-later
#include "archivebackend.h"
#include "archive7zip.h"
#include <archive.h>
#include <archive_entry.h>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <memory>
#include <cerrno>
#include <limits>
#include <sys/stat.h>

namespace ArchiveBackend {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("ArchiveBackend", text); }
struct ReadState {
    QFile file;
    QByteArray buffer{65536, Qt::Uninitialized};
    std::atomic_bool *cancel = nullptr;
};
struct Reader {
    // The archive is destroyed first, while its callback state is still alive.
    std::unique_ptr<ReadState> input;
    std::unique_ptr<struct archive, decltype(&archive_read_free)> handle{nullptr, archive_read_free};
    struct archive *get() const { return handle.get(); }
    explicit operator bool() const { return bool(handle); }
};
la_ssize_t readInput(struct archive *a, void *opaque, const void **buffer)
{
    auto *state = static_cast<ReadState *>(opaque);
    if (*state->cancel) { archive_set_error(a, ECANCELED, "Canceled"); return -1; }
    const qint64 size = state->file.read(state->buffer.data(), state->buffer.size());
    if (size < 0) archive_set_error(a, EIO, "%s", state->file.errorString().toUtf8().constData());
    *buffer = state->buffer.constData(); return size;
}
la_int64_t seekInput(struct archive *a, void *opaque, la_int64_t offset, int whence)
{
    auto *state = static_cast<ReadState *>(opaque);
    if (*state->cancel) { archive_set_error(a, ECANCELED, "Canceled"); return -1; }
    const qint64 origin = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? state->file.pos() : state->file.size();
    if (offset < -origin || offset > std::numeric_limits<qint64>::max() - origin || !state->file.seek(origin + offset)) { archive_set_error(a, EIO, "Cannot seek archive"); return -1; }
    return state->file.pos();
}
struct WriteState { QSaveFile *file; std::atomic_bool *cancel; };
la_ssize_t writeOutput(struct archive *a, void *opaque, const void *buffer, size_t length)
{
    auto *state = static_cast<WriteState *>(opaque);
    if (*state->cancel) { archive_set_error(a, ECANCELED, "Canceled"); return -1; }
    const auto size = state->file->write(static_cast<const char *>(buffer), qint64(length));
    if (size < 0) archive_set_error(a, EIO, "%s", state->file->errorString().toUtf8().constData());
    return size;
}
using Writer = std::unique_ptr<struct archive, decltype(&archive_write_free)>;
Result failed(const QString &message, bool cancelled = false)
{
    Result r; r.cancelled = cancelled;
    if (!cancelled) r.error = message;
    const QString lower = message.toLower();
    r.passwordRequired = lower.contains("passphrase") || lower.contains("password");
    r.passwordRejected = r.passwordRequired && (lower.contains("incorrect") || lower.contains("wrong") || lower.contains("bad password"));
    return r;
}
QString error(struct archive *a)
{
    const char *value = archive_error_string(a);
    return value ? QString::fromUtf8(value) : tr("The archive operation failed.");
}
Reader reader(const QString &path, const QString &password, std::atomic_bool &cancel, QString &message)
{
    Reader result;
    result.input = std::make_unique<ReadState>();
    result.input->file.setFileName(path); result.input->cancel = &cancel;
    if (!result.input->file.open(QIODevice::ReadOnly)) { message = result.input->file.errorString(); return result; }
    result.handle.reset(archive_read_new());
    auto *a = result.get();
    archive_read_support_filter_all(a);
    const QString lower = path.toLower();
    const bool stream = (lower.endsWith(".gz") || lower.endsWith(".bz2") || lower.endsWith(".xz") || lower.endsWith(".zst"))
                        && !lower.contains(".tar.");
    if (stream) archive_read_support_format_raw(a);
    else archive_read_support_format_all(a);
    if (!password.isEmpty()) archive_read_add_passphrase(a, password.toUtf8().constData());
    archive_read_set_seek_callback(a, seekInput);
    if (archive_read_open(a, result.input.get(), nullptr, readInput, nullptr) != ARCHIVE_OK) {
        message = error(a); result.handle.reset();
    }
    return result;
}
QString entryName(archive_entry *entry, struct archive *a, const QString &path)
{
    if ((archive_format(a) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_RAW)
        return QFileInfo(stem(path)).fileName();
    const char *utf8 = archive_entry_pathname_utf8(entry);
    return utf8 ? QString::fromUtf8(utf8) : QString::fromLocal8Bit(archive_entry_pathname(entry));
}
bool safePath(const QString &value, QString *clean = nullptr)
{
    QString name = value;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (name.startsWith(QLatin1Char('/')) || (name.size() > 1 && name.at(1) == QLatin1Char(':'))
        || name.split(QLatin1Char('/')).contains(QStringLiteral(".."))) return false;
    name = QDir::cleanPath(name);
    if (name.isEmpty() || name == QLatin1String(".")) return false;
    if (clean) *clean = name;
    return true;
}
bool selectedPath(const QString &path, const QStringList &selected)
{
    if (selected.isEmpty()) return true;
    for (const auto &name : selected)
        if (path == name || path.startsWith(name + QLatin1Char('/'))) return true;
    return false;
}
QString uniqueFolder(const QString &parent, const QString &name)
{
    const QString baseName = name.isEmpty() ? QStringLiteral("Archive") : name;
    QString result = baseName;
    for (int n = 2; QFileInfo::exists(QDir(parent).filePath(result)); ++n)
        result = baseName + QStringLiteral(" (%1)").arg(n);
    return result;
}
}

QString stem(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    for (const QString &suffix : {QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"),
                                 QStringLiteral(".tar.zst"), QStringLiteral(".tgz"), QStringLiteral(".tbz2"),
                                 QStringLiteral(".txz"), QStringLiteral(".zip"), QStringLiteral(".7z"),
                                 QStringLiteral(".rar"), QStringLiteral(".tar"), QStringLiteral(".gz"),
                                 QStringLiteral(".bz2"), QStringLiteral(".xz"), QStringLiteral(".zst")})
        if (name.endsWith(suffix, Qt::CaseInsensitive)) return name.left(name.size() - suffix.size());
    return QFileInfo(name).completeBaseName();
}

static Result listNative(const QString &path, const QString &password, std::atomic_bool &cancel)
{
    if (cancel) return failed({}, true);
    QString message;
    auto a = reader(path, password, cancel, message);
    if (!a) return failed(message, cancel);
    Result result;
    archive_entry *entry = nullptr;
    int code;
    while ((code = archive_read_next_header(a.get(), &entry)) == ARCHIVE_OK) {
        if (cancel) return failed({}, true);
        if ((archive_format(a.get()) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_RAW
            && archive_filter_code(a.get(), 0) == ARCHIVE_FILTER_NONE) return failed(tr("This file is not a supported archive."));
        if (result.entries.size() >= 200000) return failed(tr("The archive contains too many entries."));
        Entry item;
        item.path = entryName(entry, a.get(), path);
        while (item.path.startsWith(QLatin1String("./"))) item.path.remove(0, 2);
        item.path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        while (item.path.endsWith(QLatin1Char('/'))) item.path.chop(1);
        if (item.path.isEmpty() || item.path == QLatin1String(".")) continue;
        item.directory = archive_entry_filetype(entry) == AE_IFDIR;
        item.link = archive_entry_symlink(entry) || archive_entry_hardlink(entry);
        item.encrypted = archive_entry_is_encrypted(entry) == 1;
        item.size = qMax<qint64>(0, archive_entry_size(entry));
        item.modified = QDateTime::fromSecsSinceEpoch(archive_entry_mtime(entry));
        result.entries.append(item);
        if (archive_read_data_skip(a.get()) < ARCHIVE_OK) return failed(error(a.get()), cancel);
    }
    if (code != ARCHIVE_EOF) return failed(error(a.get()), cancel);
    return result;
}

static Result extractNative(const QString &path, const QString &parent, const QStringList &selected,
               const QString &password, std::atomic_bool &cancel, const Progress &progress)
{
    if (cancel) return failed({}, true);
    const QString base = QFileInfo(parent).canonicalFilePath();
    if (base.isEmpty() || !QFileInfo(base).isDir()) return failed(tr("Choose an existing destination folder."));
    QTemporaryDir staging(QDir(base).filePath(QStringLiteral(".karyo-extract-XXXXXX")));
    if (!staging.isValid()) return failed(tr("Cannot write to the destination folder."));
    QString message;
    auto a = reader(path, password, cancel, message);
    if (!a) return failed(message, cancel);
    std::unique_ptr<struct archive, decltype(&archive_write_free)> disk(archive_write_disk_new(), archive_write_free);
    archive_write_disk_set_options(disk.get(), ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM
        | ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    archive_entry *entry = nullptr;
    qint64 done = 0;
    int count = 0, code;
    while ((code = archive_read_next_header(a.get(), &entry)) == ARCHIVE_OK) {
        if (cancel) return failed({}, true);
        if ((archive_format(a.get()) & ARCHIVE_FORMAT_BASE_MASK) == ARCHIVE_FORMAT_RAW
            && archive_filter_code(a.get(), 0) == ARCHIVE_FILTER_NONE) return failed(tr("This file is not a supported archive."));
        QString name;
        const QString original = entryName(entry, a.get(), path);
        if (original == QLatin1String(".") || original == QLatin1String("./")) continue;
        if (!safePath(original, &name)) return failed(tr("The archive contains an unsafe path: %1").arg(original));
        if (!selectedPath(name, selected)) {
            if (archive_read_data_skip(a.get()) < ARCHIVE_OK) return failed(error(a.get()), cancel);
            continue;
        }
        // Some libarchive RAR decoders report opaque decompression errors before
        // recognizing encrypted data. Route known encrypted entries explicitly.
        const int family = archive_format(a.get()) & ARCHIVE_FORMAT_BASE_MASK;
        if (archive_entry_is_encrypted(entry) == 1 && (family == ARCHIVE_FORMAT_RAR || family == ARCHIVE_FORMAT_RAR_V5))
            return failed(QStringLiteral("RAR encryption is not supported by libarchive."));
        const auto type = archive_entry_filetype(entry);
        if (type != AE_IFREG && type != AE_IFDIR && type != AE_IFLNK && !archive_entry_hardlink(entry))
            return failed(tr("Unsupported special file in archive: %1").arg(name));
        if (const char *link = archive_entry_symlink(entry)) {
            const QString target = QString::fromLocal8Bit(link);
            const QString resolved = QDir::cleanPath(QFileInfo(name).path() + QLatin1Char('/') + target);
            if (QDir::isAbsolutePath(target) || !safePath(resolved))
                return failed(tr("The archive contains an unsafe link: %1").arg(name));
        }
        if (const char *link = archive_entry_hardlink(entry)) {
            QString target;
            if (!safePath(QString::fromLocal8Bit(link), &target))
                return failed(tr("The archive contains an unsafe link: %1").arg(name));
            archive_entry_set_hardlink(entry, QFile::encodeName(QDir(staging.path()).filePath(target)).constData());
        }
        archive_entry_set_pathname(entry, QFile::encodeName(QDir(staging.path()).filePath(name)).constData());
        // Do not restore ownership or setuid/setgid/sticky bits from an archive.
        archive_entry_set_perm(entry, archive_entry_perm(entry) & 0777);
        if (archive_write_header(disk.get(), entry) < ARCHIVE_OK) return failed(error(disk.get()));
        const void *buffer;
        size_t size;
        la_int64_t offset;
        while ((code = archive_read_data_block(a.get(), &buffer, &size, &offset)) == ARCHIVE_OK) {
            if (cancel) return failed({}, true);
            if (archive_write_data_block(disk.get(), buffer, size, offset) < ARCHIVE_OK)
                return failed(error(disk.get()));
            done += qint64(size);
            if (progress) progress(done, 0, name);
        }
        if (code != ARCHIVE_EOF) return failed(error(a.get()), cancel);
        if (archive_write_finish_entry(disk.get()) < ARCHIVE_OK) return failed(error(disk.get()));
        ++count;
    }
    if (code != ARCHIVE_EOF) return failed(error(a.get()), cancel);
    if (archive_write_close(disk.get()) < ARCHIVE_OK) return failed(error(disk.get()));
    if (cancel) return failed({}, true);
    if (!count) return failed(tr("No files were selected for extraction."));
    const QString destination = QDir(base).filePath(uniqueFolder(base, stem(path)));
    if (!QDir().rename(staging.path(), destination)) return failed(tr("Cannot finish extracting the archive."));
    staging.setAutoRemove(false);
    Result result; result.output = destination; return result;
}

Result list(const QString &path, const QString &password, std::atomic_bool &cancel)
{
    Result result = listNative(path, password, cancel);
    return SevenZip::needed(result) ? SevenZip::list(path, password, cancel) : result;
}

Result extract(const QString &path, const QString &parent, const QStringList &selected,
               const QString &password, std::atomic_bool &cancel, const Progress &progress)
{
    Result result = extractNative(path, parent, selected, password, cancel, progress);
    return SevenZip::needed(result) ? SevenZip::extract(path, parent, selected, password, cancel, progress) : result;
}

Result create(const QStringList &sources, const QString &output, Format format,
              std::atomic_bool &cancel, const Progress &progress, const QString &password, bool encryptNames, int compressionLevel)
{
    if (cancel) return failed({}, true);
    if (!password.isEmpty() && format == Format::TarGz)
        return failed(tr("Password protection is available for ZIP and 7z archives."));
    if (compressionLevel < -1 || compressionLevel > 9)
        return failed(tr("Choose a compression level between 0 and 9."));
    if (sources.isEmpty()) return failed(tr("Choose files or folders to archive."));
    struct Source { QString path, name; };
    QList<Source> files;
    QSet<QString> roots;
    qint64 total = 0;
    for (const QString &source : sources) {
        if (cancel) return failed({}, true);
        const QFileInfo info(source);
        if (!info.exists() && !info.isSymLink()) return failed(tr("A source file no longer exists: %1").arg(source));
        if (roots.contains(info.fileName())) return failed(tr("Two source items have the same name: %1").arg(info.fileName()));
        roots.insert(info.fileName());
        const QString absolute = info.absoluteFilePath();
        if (QFileInfo(output).absoluteFilePath() == absolute
            || (info.isDir() && QFileInfo(output).absoluteFilePath().startsWith(absolute + QLatin1Char('/'))))
            return failed(tr("Save the archive outside the source folders."));
        files.append({absolute, info.fileName()});
        if (!info.isSymLink() && info.isDir()) {
            QDirIterator it(absolute, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (cancel) return failed({}, true);
                const QString child = it.next();
                files.append({child, info.fileName() + QLatin1Char('/') + QDir(absolute).relativeFilePath(child)});
                if (files.size() > 200000) return failed(tr("Too many source files."));
            }
        }
    }
    for (const auto &file : files) if (QFileInfo(file.path).isFile() && !QFileInfo(file.path).isSymLink()) total += QFileInfo(file.path).size();
    if (!password.isEmpty() && format == Format::SevenZip) {
        for (const Source &file : files) {
            const QFileInfo info(file.path);
            if (info.isSymLink()) return failed(tr("Links in protected archives are not supported."));
            if (!info.isFile() && !info.isDir()) return failed(tr("Unsupported special file: %1").arg(file.name));
        }
        return SevenZip::create(sources, output, password, encryptNames, cancel, progress, compressionLevel);
    }
    QSaveFile destination(output);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly)) return failed(destination.errorString());
    WriteState state{&destination, &cancel};
    Writer a(archive_write_new(), archive_write_free);
    int code;
    if (format == Format::Zip) code = archive_write_set_format_zip(a.get());
    else if (format == Format::SevenZip) code = archive_write_set_format_7zip(a.get());
    else {
        code = archive_write_set_format_pax_restricted(a.get());
        if (code == ARCHIVE_OK) code = archive_write_add_filter_gzip(a.get());
    }
    if (code == ARCHIVE_OK && compressionLevel >= 0) {
        const QByteArray level = QByteArray::number(compressionLevel);
        if (format == Format::TarGz)
            code = archive_write_set_filter_option(a.get(), "gzip", "compression-level", level.constData());
        else {
            const char *module = format == Format::Zip ? "zip" : "7zip";
            code = archive_write_set_format_option(a.get(), module, "compression-level", level.constData());
            if (code == ARCHIVE_OK && format == Format::SevenZip && compressionLevel == 0)
                code = archive_write_set_format_option(a.get(), module, "compression", "store");
        }
    }
    if (code == ARCHIVE_OK && !password.isEmpty()) {
        code = archive_write_set_passphrase(a.get(), password.toUtf8().constData());
        if (code == ARCHIVE_OK)
            code = archive_write_set_format_option(a.get(), "zip", "encryption", "aes256");
    }
    if (code != ARCHIVE_OK || archive_write_open(a.get(), &state, nullptr, writeOutput, nullptr) != ARCHIVE_OK)
        return failed(error(a.get()));
    qint64 done = 0;
    QByteArray buffer(65536, Qt::Uninitialized);
    for (const Source &source : files) {
        if (cancel) return failed({}, true);
        struct stat st{};
        if (lstat(QFile::encodeName(source.path).constData(), &st) != 0)
            return failed(tr("Cannot read source: %1").arg(source.path));
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
            return failed(tr("Unsupported special file: %1").arg(source.path));
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        archive_entry_set_pathname_utf8(entry.get(), source.name.toUtf8().constData());
        archive_entry_set_filetype(entry.get(), st.st_mode & S_IFMT);
        archive_entry_set_perm(entry.get(), st.st_mode & 0777);
        archive_entry_set_mtime(entry.get(), st.st_mtime, 0);
        archive_entry_set_size(entry.get(), S_ISREG(st.st_mode) ? st.st_size : 0);
        if (S_ISLNK(st.st_mode)) {
            const QString target = QFileInfo(source.path).symLinkTarget();
            const QString relative = QDir(QFileInfo(source.path).absolutePath()).relativeFilePath(target);
            if (!safePath(QDir::cleanPath(QFileInfo(source.name).path() + QLatin1Char('/') + relative)))
                return failed(tr("A symbolic link points outside the archive: %1").arg(source.name));
            archive_entry_set_symlink_utf8(entry.get(), relative.toUtf8().constData());
        }
        if (archive_write_header(a.get(), entry.get()) < ARCHIVE_OK) return failed(error(a.get()));
        if (S_ISREG(st.st_mode)) {
            QFile input(source.path);
            if (!input.open(QIODevice::ReadOnly)) return failed(input.errorString());
            qint64 remaining = st.st_size;
            while (remaining > 0) {
                if (cancel) return failed({}, true);
                const qint64 size = input.read(buffer.data(), qMin<qint64>(remaining, buffer.size()));
                if (size <= 0) return failed(tr("A source file changed or could not be read: %1").arg(source.path));
                if (archive_write_data(a.get(), buffer.constData(), size) != size) return failed(error(a.get()));
                remaining -= size; done += size;
                if (progress) progress(done, total, source.name);
            }
        }
        if (archive_write_finish_entry(a.get()) < ARCHIVE_OK) return failed(error(a.get()));
    }
    if (archive_write_close(a.get()) < ARCHIVE_OK) return failed(error(a.get()), cancel);
    if (cancel) return failed({}, true);
    if (!destination.commit()) return failed(destination.errorString());
    Result result; result.output = output; return result;
}
}
