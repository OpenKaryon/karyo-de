// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the archive jobs in a temporary folder: compress to ZIP and TAR.GZ,
// extract them again (one folder inside comes out as it is, several items
// go into a folder named after the archive), gzip a file and decompress it.
#include "archives.h"

#include <QCoreApplication>
#include <archive.h>
#include <archive_entry.h>
#include <atomic>
#include "archivebackend.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

int failures = 0;

void expect(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

QString wait(ArchiveJob *job)
{
    QString result;
    QString error;
    QEventLoop loop;
    QObject::connect(job, &ArchiveJob::finished, &loop, [&](const QString &r, const QString &e) {
        result = r;
        error = e;
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, [&] { error = "Archive job timeout"; loop.quit(); });
    loop.exec();
    if (!error.isEmpty())
        std::printf("  error: %s\n", qPrintable(error));
    return result;
}

void write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(data);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return file.readAll();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (!Archives::available()) {
        std::printf("FAIL archive support is unavailable\n");
        return 1;
    }
    expect(Archives::kindOf(QStringLiteral("a.tar.gz")) == Archives::Kind::Archive, "tar.gz is an archive");
    expect(Archives::kindOf(QStringLiteral("reads.fastq.gz")) == Archives::Kind::Compressed, "fastq.gz is compressed");
    expect(Archives::kindOf(QStringLiteral("notes.txt")) == Archives::Kind::None, "txt is neither");
    expect(Archives::stem(QStringLiteral("/x/run.tar.gz")) == QLatin1String("run"), "stem of tar.gz");
    expect(Archives::stem(QStringLiteral("reads.fastq.gz")) == QLatin1String("reads.fastq"), "stem of fastq.gz");

    QTemporaryDir temp;
    QDir dir(temp.path());
    dir.mkpath(QStringLiteral("project/data"));
    write(dir.filePath(QStringLiteral("project/data/counts.tsv")), "gene\tcount\nHSP104\t120\n");
    write(dir.filePath(QStringLiteral("project/Snakefile")), "rule all:\n");
    write(dir.filePath(QStringLiteral("notes.txt")), "notes\n");

    // One folder: zip and tar.gz, then out again beside a renamed original.
    const QString zip = wait(ArchiveJob::compress({dir.filePath(QStringLiteral("project"))}, ArchiveJob::Format::Zip));
    expect(zip == dir.filePath(QStringLiteral("project.zip")) && QFile::exists(zip), "zip made");
    const QString tgz = wait(ArchiveJob::compress({dir.filePath(QStringLiteral("project"))}, ArchiveJob::Format::TarGz));
    expect(tgz == dir.filePath(QStringLiteral("project.tar.gz")) && QFile::exists(tgz), "tar.gz made");
    dir.mkdir(QStringLiteral("out"));
    const QString fromZip = wait(ArchiveJob::extract(zip, dir.filePath(QStringLiteral("out"))));
    expect(fromZip == dir.filePath(QStringLiteral("out/project")), "one folder comes out as it is");
    expect(read(dir.filePath(QStringLiteral("out/project/data/counts.tsv"))).contains("HSP104"), "zip content");
    const QString fromTgz = wait(ArchiveJob::extract(tgz, dir.filePath(QStringLiteral("out"))));
    expect(fromTgz == dir.filePath(QStringLiteral("out/project (2)")), "a second copy takes a free name");
    expect(QDir(dir.filePath(QStringLiteral("out")))
                   .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
                   .size() == 2,
           "no temporary folder is left");

    // Several items: a folder named after the archive.
    const QString both = wait(ArchiveJob::compress(
        {dir.filePath(QStringLiteral("notes.txt")), dir.filePath(QStringLiteral("project"))}, ArchiveJob::Format::Zip));
    expect(both == dir.filePath(QStringLiteral("notes.zip")), "named after the first item");
    const QString fromBoth = wait(ArchiveJob::extract(both, dir.filePath(QStringLiteral("out"))));
    expect(fromBoth == dir.filePath(QStringLiteral("out/notes")), "several items go into a folder");
    expect(QFile::exists(dir.filePath(QStringLiteral("out/notes/project/Snakefile"))), "folder content");

    // A single file: gzip and back.
    const QString gz = wait(ArchiveJob::compress({dir.filePath(QStringLiteral("notes.txt"))}, ArchiveJob::Format::Gzip));
    expect(gz == dir.filePath(QStringLiteral("notes.txt.gz")), "gzip made");
    QFile::rename(dir.filePath(QStringLiteral("notes.txt")), dir.filePath(QStringLiteral("notes.orig")));
    const QString plain = wait(ArchiveJob::decompress(gz));
    expect(plain == dir.filePath(QStringLiteral("notes.txt")) && read(plain) == "notes\n", "decompressed");

    // Invalid input finishes asynchronously and never publishes an artifact.
    expect(wait(ArchiveJob::compress({}, ArchiveJob::Format::Zip)).isEmpty(), "empty selection fails");

    // A broken archive: an error, and nothing left behind.
    write(dir.filePath(QStringLiteral("broken.zip")), "not a zip");
    const int before = int(dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size());
    const QString broken = wait(ArchiveJob::extract(dir.filePath(QStringLiteral("broken.zip")), dir.path()));
    expect(broken.isEmpty(), "a broken archive fails");
    expect(dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).size() == before,
           "a failed extraction leaves nothing");

    // The quick path must enforce the same traversal protections as Archive UI.
    const QString unsafe = dir.filePath(QStringLiteral("unsafe.tar"));
    struct archive *writer = archive_write_new();
    archive_write_set_format_pax_restricted(writer);
    expect(archive_write_open_filename(writer, QFile::encodeName(unsafe).constData()) == ARCHIVE_OK, "unsafe fixture opens");
    auto *entry = archive_entry_new();
    archive_entry_set_pathname(entry, "../escaped.txt"); archive_entry_set_size(entry, 3);
    archive_entry_set_filetype(entry, AE_IFREG); archive_entry_set_perm(entry, 0600);
    expect(archive_write_header(writer, entry) == ARCHIVE_OK, "unsafe fixture header");
    expect(archive_write_data(writer, "bad", 3) == 3, "unsafe fixture data");
    archive_entry_free(entry); archive_write_close(writer); archive_write_free(writer);
    expect(wait(ArchiveJob::extract(unsafe, dir.filePath("out"))).isEmpty(), "traversal is rejected");
    expect(!QFileInfo::exists(dir.filePath("escaped.txt")), "no escaped payload");

    // Protected archives fail visibly without publishing an incomplete folder.
    std::atomic_bool cancel{false};
    const QString protectedZip = dir.filePath("protected.zip");
    const auto protectedResult = ArchiveBackend::create({dir.filePath("notes.orig")}, protectedZip,
        ArchiveBackend::Format::Zip, cancel, {}, QStringLiteral("public-test-password"));
    expect(protectedResult.ok(), "protected fixture created");
    expect(wait(ArchiveJob::extract(protectedZip, dir.filePath("out"))).isEmpty(), "protected quick extract needs password");
    expect(!QFileInfo::exists(dir.filePath("out/protected")), "no protected partial folder");

    // Missing/failed stream tools are asynchronous failures, preserving old files.
    const QByteArray oldPath = qgetenv("PATH");
    dir.mkdir("tools");
    const QString tools = dir.filePath("tools");
    qputenv("PATH", tools.toUtf8());
    expect(wait(ArchiveJob::compress({dir.filePath("notes.orig")}, ArchiveJob::Format::Gzip)).isEmpty(), "missing gzip fails");
    write(tools + "/gzip", "#!/bin/sh\nexit 1\n");
    QFile::setPermissions(tools + "/gzip", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    write(dir.filePath("notes.orig.gz"), "keep");
    expect(wait(ArchiveJob::compress({dir.filePath("notes.orig")}, ArchiveJob::Format::Gzip)).isEmpty(), "silent tool failure fails");
    expect(read(dir.filePath("notes.orig.gz")) == "keep", "existing output preserved");
    expect(!QFileInfo::exists(dir.filePath("notes.orig (2).gz")), "failed output unpublished");
    qputenv("PATH", oldPath);

    if (failures == 0)
        std::printf("all passed\n");
    return failures == 0 ? 0 : 1;
}
