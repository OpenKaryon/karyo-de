// SPDX-License-Identifier: GPL-3.0-or-later
#include "archive7zip.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <sys/stat.h>
#include <utility>

namespace ArchiveBackend::SevenZip {
namespace {
QString tr(const char *s) { return QCoreApplication::translate("ArchiveBackend", s); }
Result failure(const QString &message) { Result r; r.error = message; return r; }
struct Command { Result result; QByteArray output; };
Command execute(QStringList args, const QString &password, std::atomic_bool &cancel)
{
    Command result;
    if (cancel) { result.result.cancelled = true; return result; }
    QString executable = QStandardPaths::findExecutable(QStringLiteral("7zz"));
    if (executable.isEmpty()) executable = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (executable.isEmpty()) { result.result = failure(tr("7-Zip is not available.")); return result; }
    if (password.contains(QLatin1Char('\n')) || password.contains(QLatin1Char('\r')) || password.contains(QChar::Null)) {
        result.result = failure(tr("This password contains characters that 7-Zip cannot accept.")); return result;
    }
    for (const QString &arg : {QStringLiteral("-sccUTF-8"), QStringLiteral("-bsp0"), QStringLiteral("-bb0"), QStringLiteral("-mmt=2")})
        args.insert(1, arg);
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    process.setProcessEnvironment(environment);
    process.start(executable, args);
    if (!process.waitForStarted(5000)) { result.result = failure(process.errorString()); return result; }
    // Never expose the password in argv, environment, a list file or a log.
    // Reading omits -p so 7-Zip requests stdin; creation uses bare -p to enable it.
    process.write(password.toUtf8() + '\n');
    process.closeWriteChannel();
    QByteArray errors;
    do {
        process.waitForFinished(50);
        result.output += process.readAllStandardOutput();
        errors += process.readAllStandardError();
        if (cancel || result.output.size() > 64 * 1024 * 1024 || errors.size() > 1024 * 1024) {
            process.kill(); process.waitForFinished();
            result.result.cancelled = cancel;
            if (!cancel) result.result.error = tr("The archive contains too many entries.");
            return result;
        }
    } while (process.state() != QProcess::NotRunning);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString message = QString::fromUtf8(errors).trimmed();
        const QString lower = message.toLower();
        result.result.passwordRequired = lower.contains(QLatin1String("wrong password"))
            || lower.contains(QLatin1String("password is incorrect"));
        result.result.passwordRejected = result.result.passwordRequired && !password.isEmpty();
        result.result.error = result.result.passwordRequired ? tr("The archive password is required or incorrect.")
            : message.isEmpty() ? tr("The archive operation failed.") : message;
    }
    return result;
}
bool safePath(QString name)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return !name.isEmpty() && name != QLatin1String(".") && !name.startsWith(QLatin1Char('/'))
        && !(name.size() > 1 && name.at(1) == QLatin1Char(':'))
        && !name.split(QLatin1Char('/')).contains(QStringLiteral(".."));
}
bool wanted(const QString &path, const QStringList &selected)
{
    if (selected.isEmpty()) return true;
    for (const QString &item : selected)
        if (path == item || path.startsWith(item + QLatin1Char('/'))) return true;
    return false;
}
}

bool needed(const Result &result)
{
    const QString error = result.error.toLower();
    return !result.cancelled && !result.ok()
        && (error.contains(QLatin1String("encrypt")))
        && (error.contains(QLatin1String("not supported")) || error.contains(QLatin1String("unsupported"))
            || error.contains(QLatin1String("unavailable")));
}

Result list(const QString &path, const QString &password, std::atomic_bool &cancel)
{
    auto command = execute({QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-ba"),
        QStringLiteral("--"), QFileInfo(path).absoluteFilePath()}, password, cancel);
    if (!command.result.ok()) return command.result;
    Result result;
    QMap<QString, QString> fields;
    auto append = [&]() -> bool {
        if (fields.isEmpty()) return true;
        bool valid = false;
        Entry entry; entry.path = fields.value(QStringLiteral("Path"));
        entry.path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        entry.size = fields.value(QStringLiteral("Size")).toLongLong(&valid);
        if (!valid || entry.size < 0 || !safePath(entry.path) || result.entries.size() >= 200000) {
            result.error = tr("The archive contains an unsafe or unsupported entry."); return false;
        }
        const QString attributes = fields.value(QStringLiteral("Attributes"));
        entry.directory = fields.value(QStringLiteral("Folder")) == QLatin1String("+") || attributes.startsWith(QLatin1Char('D'));
        entry.link = attributes.contains(QLatin1String(" lrwx")) || fields.contains(QStringLiteral("Symbolic Link"))
            || fields.contains(QStringLiteral("Hard Link"));
        entry.encrypted = fields.value(QStringLiteral("Encrypted")) == QLatin1String("+");
        entry.modified = QDateTime::fromString(fields.value(QStringLiteral("Modified")).left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        result.entries.append(entry); fields.clear(); return true;
    };
    for (QByteArray line : command.output.split('\n')) {
        if (cancel) { result.cancelled = true; return result; }
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) { if (!append()) return result; continue; }
        const int equals = line.indexOf(" = ");
        if (equals < 0) {
            if (fields.isEmpty() && line == "Enter password (will not be echoed):") continue;
            return failure(tr("The archive contains an unsafe or unsupported entry."));
        }
        const QString key = QString::fromUtf8(line.left(equals));
        if (fields.contains(key)) return failure(tr("The archive contains an unsafe or unsupported entry."));
        fields.insert(key, QString::fromUtf8(line.mid(equals + 3)));
    }
    append(); return result;
}

Result extract(const QString &path, const QString &parent, const QStringList &selected,
               const QString &password, std::atomic_bool &cancel, const Progress &progress)
{
    Result contents = list(path, password, cancel);
    if (!contents.ok()) return contents;
    const QString base = QFileInfo(parent).canonicalFilePath();
    if (base.isEmpty() || !QFileInfo(base).isDir()) return failure(tr("Choose an existing destination folder."));
    QStringList names;
    for (const Entry &entry : std::as_const(contents.entries)) {
        if (entry.link) return failure(tr("Links in protected archives are not supported."));
        if (wanted(entry.path, selected)) names.append(entry.path);
    }
    if (names.isEmpty()) return failure(tr("No files were selected for extraction."));
    QTemporaryDir staging(QDir(base).filePath(QStringLiteral(".karyo-extract-XXXXXX")));
    if (!staging.isValid()) return failure(tr("Cannot write to the destination folder."));
    // 7-Zip's own path/link protection stays enabled; preflight every entry,
    // and publish only a checked, link-free staging tree after successful decode.
    QStringList args{QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-spd"),
        QStringLiteral("-o") + staging.path(), QStringLiteral("--"), QFileInfo(path).absoluteFilePath()};
    // Select exact listed entries, not directory/wildcard matches.
    // Use a bounded UTF-8 list file so huge selections never overflow argv.
    QTemporaryDir selection;
    QFile selectionFile(selection.filePath(QStringLiteral("entries.txt")));
    if (!selected.isEmpty()) {
        for (const QString &name : names)
            if (name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r')))
                return failure(tr("The archive contains an unsafe or unsupported entry."));
        if (!selection.isValid() || !selectionFile.open(QIODevice::WriteOnly)) return failure(tr("The archive operation failed."));
        const QByteArray selectionData = names.join(QLatin1Char('\n')).toUtf8();
        if (selectionFile.write(selectionData) != selectionData.size() || !selectionFile.flush())
            return failure(tr("The archive operation failed."));
        selectionFile.close();
        args.insert(1, QStringLiteral("-scsUTF-8"));
        args.insert(1, QStringLiteral("-i@") + selectionFile.fileName());
    }
    if (progress) progress(0, 0, QFileInfo(path).fileName());
    auto command = execute(args, password, cancel);
    if (!command.result.ok()) return command.result;
    QDirIterator files(staging.path(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        if (cancel) { Result r; r.cancelled = true; return r; }
        const QFileInfo info(files.next());
        if (info.isSymLink() || (!info.isFile() && !info.isDir())) return failure(tr("The archive contains an unsafe or unsupported entry."));
        struct stat attributes{};
        const QByteArray filename = QFile::encodeName(info.absoluteFilePath());
        if (lstat(filename.constData(), &attributes) != 0 || chmod(filename.constData(), attributes.st_mode & 0777) != 0)
            return failure(tr("The archive operation failed."));
    }
    if (cancel) { Result r; r.cancelled = true; return r; }
    QString name = stem(path);
    if (name.isEmpty()) name = QStringLiteral("Archive");
    QString destination = QDir(base).filePath(name);
    for (int n = 2; QFileInfo::exists(destination); ++n)
        destination = QDir(base).filePath(name + QStringLiteral(" (%1)").arg(n));
    if (!QDir().rename(staging.path(), destination)) return failure(tr("Cannot finish extracting the archive."));
    staging.setAutoRemove(false);
    Result result; result.output = destination; return result;
}

Result create(const QStringList &sources, const QString &output, const QString &password,
              bool encryptNames, std::atomic_bool &cancel, const Progress &progress, int compressionLevel)
{
    QTemporaryDir staging(QDir(QFileInfo(output).absolutePath()).filePath(QStringLiteral(".karyo-create-XXXXXX")));
    if (!staging.isValid()) return failure(tr("Cannot write to the destination folder."));
    const QString temporary = staging.filePath(QStringLiteral("archive.7z"));
    QStringList args{QStringLiteral("a"), QStringLiteral("-t7z"), QStringLiteral("-p"),
        encryptNames ? QStringLiteral("-mhe=on") : QStringLiteral("-mhe=off"),
        QStringLiteral("-y"), temporary, QStringLiteral("--")};
    if (compressionLevel >= 0) args.insert(1, QStringLiteral("-mx=%1").arg(compressionLevel));
    for (const QString &source : sources) args.append(QFileInfo(source).absoluteFilePath());
    if (progress) progress(0, 0, QFileInfo(output).fileName());
    auto command = execute(args, password, cancel);
    if (!command.result.ok()) return command.result;
    QFile input(temporary); QSaveFile destination(output); destination.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) return failure(tr("The archive operation failed."));
    QByteArray buffer(65536, Qt::Uninitialized);
    while (!input.atEnd()) {
        if (cancel) { Result r; r.cancelled = true; return r; }
        const qint64 size = input.read(buffer.data(), buffer.size());
        if (size < 0 || destination.write(buffer.constData(), size) != size) return failure(tr("The archive operation failed."));
    }
    if (cancel) { Result r; r.cancelled = true; return r; }
    if (!destination.commit()) return failure(destination.errorString());
    Result result; result.output = output; return result;
}
}
