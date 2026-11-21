// SPDX-License-Identifier: GPL-3.0-or-later
#include "archivebackend.h"
#include "archive7zip.h"
#include <archive.h>
#include <archive_entry.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace ArchiveBackend;
class ArchiveBackendTest : public QObject
{
    Q_OBJECT
    bool write(const QString &path, const QByteArray &data)
    {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
    QByteArray read(const QString &path)
    {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
    void unsafeArchive(const QString &output, const QByteArray &name, const QByteArray &link = {}, bool hard = false)
    {
        auto *a = archive_write_new(); archive_write_set_format_pax_restricted(a);
        QCOMPARE(archive_write_open_filename(a, QFile::encodeName(output).constData()), ARCHIVE_OK);
        auto *entry = archive_entry_new(); archive_entry_set_pathname(entry, name.constData());
        archive_entry_set_filetype(entry, link.isEmpty() || hard ? AE_IFREG : AE_IFLNK);
        archive_entry_set_perm(entry, 0644); archive_entry_set_size(entry, link.isEmpty() ? 4 : 0);
        if (!link.isEmpty()) {
            if (hard) archive_entry_set_hardlink(entry, link.constData());
            else archive_entry_set_symlink(entry, link.constData());
        }
        QCOMPARE(archive_write_header(a, entry), ARCHIVE_OK);
        if (link.isEmpty()) QCOMPARE(archive_write_data(a, "evil", 4), la_ssize_t(4));
        archive_entry_free(entry); archive_write_close(a); archive_write_free(a);
    }
private Q_SLOTS:
    void roundTrip_data()
    {
        QTest::addColumn<int>("format"); QTest::addColumn<QString>("suffix");
        QTest::newRow("zip") << int(Format::Zip) << ".zip";
        QTest::newRow("tar-gz") << int(Format::TarGz) << ".tar.gz";
        QTest::newRow("7z") << int(Format::SevenZip) << ".7z";
    }
    void roundTrip()
    {
        QFETCH(int, format); QFETCH(QString, suffix);
        QTemporaryDir root;
        const QString folder = root.filePath(QStringLiteral("Проект с пробелами"));
        QVERIFY(QDir().mkpath(folder + "/nested"));
        const QByteArray data(200000, '\x7f');
        QVERIFY(write(folder + QStringLiteral("/nested/имя\nфайла.txt"), data));
        QVERIFY(write(folder + "/one.txt", "keep"));
        const QString archive = root.filePath("bundle" + suffix);
        std::atomic_bool cancel = false;
        auto made = create({folder}, archive, Format(format), cancel);
        QVERIFY2(made.ok(), qPrintable(made.error));
        auto contents = list(archive, {}, cancel);
        QVERIFY2(contents.ok(), qPrintable(contents.error));
        QVERIFY(contents.entries.size() >= 4);
        const QString destination = root.filePath("out"); QVERIFY(QDir().mkdir(destination));
        auto result = extract(archive, destination, {}, {}, cancel);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(read(result.output + '/' + QFileInfo(folder).fileName() + QStringLiteral("/nested/имя\nфайла.txt")), data);
        // A second extraction creates another folder and preserves the first.
        auto selected = extract(archive, destination, {QFileInfo(folder).fileName() + "/nested"}, {}, cancel);
        QVERIFY2(selected.ok(), qPrintable(selected.error));
        QVERIFY(selected.output != result.output);
        QVERIFY(!QFileInfo::exists(selected.output + '/' + QFileInfo(folder).fileName() + "/one.txt"));
        QCOMPARE(read(result.output + '/' + QFileInfo(folder).fileName() + "/one.txt"), QByteArray("keep"));
    }
    void pathsAndLinksCannotEscape_data()
    {
        QTest::addColumn<QByteArray>("name"); QTest::addColumn<QByteArray>("link"); QTest::addColumn<bool>("hard");
        QTest::newRow("parent") << QByteArray("../escaped") << QByteArray() << false;
        QTest::newRow("absolute") << QByteArray("/tmp/karyo-unwanted-escape") << QByteArray() << false;
        QTest::newRow("windows-parent") << QByteArray("..\\escaped") << QByteArray() << false;
        QTest::newRow("symlink") << QByteArray("link") << QByteArray("../../escaped") << false;
        QTest::newRow("hardlink") << QByteArray("hardlink") << QByteArray("../escaped") << true;
    }
    void pathsAndLinksCannotEscape()
    {
        QFETCH(QByteArray, name); QFETCH(QByteArray, link); QFETCH(bool, hard);
        QTemporaryDir root;
        const QString archive = root.filePath("unsafe.tar"); unsafeArchive(archive, name, link, hard);
        const QString out = root.filePath("out"); QVERIFY(QDir().mkdir(out));
        std::atomic_bool cancel = false;
        const auto result = extract(archive, out, {}, {}, cancel);
        QVERIFY(!result.ok()); QVERIFY(!result.error.isEmpty());
        QVERIFY(QDir(out).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath("escaped")));
    }
    void unnamedArchiveKeepsExistingDestination()
    {
        QTemporaryDir root;
        QVERIFY(write(root.filePath("file"), "source"));
        QVERIFY(QDir().mkpath(root.filePath("Archive")));
        QVERIFY(write(root.filePath("Archive/sentinel"), "keep"));
        std::atomic_bool cancel = false;
        const QString archive = root.filePath(".zip");
        QVERIFY(create({root.filePath("file")}, archive, Format::Zip, cancel).ok());
        const auto result = extract(archive, root.path(), {}, {}, cancel);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.output, root.filePath("Archive (2)"));
        QCOMPARE(read(root.filePath("Archive/sentinel")), QByteArray("keep"));
        QCOMPARE(read(result.output + "/file"), QByteArray("source"));
    }
    void cancelAndFailurePreserveExistingArchive()
    {
        QTemporaryDir root;
        const QString input = root.filePath("large.bin"), output = root.filePath("existing.zip");
        QVERIFY(write(input, QByteArray(2 << 20, '\x47'))); QVERIFY(write(output, "original"));
        std::atomic_bool cancel = false;
        auto result = create({input}, output, Format::Zip, cancel,
            [&cancel](qint64, qint64, const QString &) { cancel = true; });
        QVERIFY(result.cancelled); QCOMPARE(read(output), QByteArray("original"));
        cancel = false;
        result = create({root.filePath("missing")}, output, Format::Zip, cancel);
        QVERIFY(!result.ok()); QCOMPARE(read(output), QByteArray("original"));
        result = create({root.path()}, root.filePath("recursive.zip"), Format::Zip, cancel);
        QVERIFY(!result.ok());
    }
    void encryptedZip_data()
    {
        QTest::addColumn<QString>("encryption");
        QTest::newRow("aes256") << "AES256";
        QTest::newRow("zipcrypto") << "ZipCrypto";
    }
    void encryptedZip()
    {
        QFETCH(QString, encryption);
        const QString seven = QStandardPaths::findExecutable("7z");
        QVERIFY2(!seven.isEmpty(), "Install 7zip to exercise encrypted archive support");
        QTemporaryDir root;
        const QString source = root.filePath("secret.txt"), archive = root.filePath("secret.zip");
        QVERIFY(write(source, "private test fixture"));
        QCOMPARE(QProcess::execute(seven, {"a", "-tzip", "-pfixture-password", "-mem=" + encryption, archive, source}), 0);
        std::atomic_bool cancel = false;
        auto result = extract(archive, root.path(), {}, {}, cancel);
        QVERIFY(!result.ok()); QVERIFY(result.passwordRequired);
        result = extract(archive, root.path(), {}, "wrong", cancel);
        QVERIFY(!result.ok()); QVERIFY(result.passwordRequired);
        result = extract(archive, root.path(), {}, "fixture-password", cancel);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(read(result.output + "/secret.txt"), QByteArray("private test fixture"));
    }
    void protectedRoundTrip_data()
    {
        QTest::addColumn<int>("format"); QTest::addColumn<bool>("headers"); QTest::addColumn<QString>("secret");
        QTest::newRow("zip-ascii") << int(Format::Zip) << false << "fixture-password";
        QTest::newRow("zip-unicode") << int(Format::Zip) << false << QStringLiteral("пароль 日本語");
        QTest::newRow("7z-hidden") << int(Format::SevenZip) << true << "fixture-password";
        QTest::newRow("7z-visible") << int(Format::SevenZip) << false << "fixture-password";
        QTest::newRow("7z-unicode") << int(Format::SevenZip) << true << QStringLiteral("пароль 日本語");
    }
    void protectedRoundTrip()
    {
        QFETCH(int, format); QFETCH(bool, headers); QFETCH(QString, secret);
        QTemporaryDir root; QVERIFY(QDir().mkpath(root.filePath("данные/nested")));
        QVERIFY(write(root.filePath("данные/nested/секрет.txt"), "private data"));
        QVERIFY(write(root.filePath("данные/other.txt"), "other"));
        const QString archive = root.filePath(format == int(Format::Zip) ? "protected.zip" : "protected.7z");
        std::atomic_bool cancel = false;
        auto result = create({root.filePath("данные")}, archive, Format(format), cancel, {}, secret, headers);
        QVERIFY2(result.ok(), qPrintable(result.error));
        auto contents = list(archive, secret, cancel);
        QVERIFY2(contents.ok(), qPrintable(contents.error)); QVERIFY(contents.entries.size() >= 3);
        if (format == int(Format::SevenZip) && headers) {
            result = list(archive, {}, cancel); QVERIFY(result.passwordRequired); QVERIFY(!result.passwordRejected);
            result = list(archive, "wrong", cancel); QVERIFY(result.passwordRequired); QVERIFY(result.passwordRejected);
        }
        const QString out = root.filePath("out"); QVERIFY(QDir().mkdir(out));
        result = extract(archive, out, {}, {}, cancel);
        QVERIFY2(result.passwordRequired, qPrintable(result.error)); QVERIFY(!result.passwordRejected);
        QVERIFY(QDir(out).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
        result = extract(archive, out, {}, "wrong", cancel);
        QVERIFY2(result.passwordRequired, qPrintable(result.error)); QVERIFY(result.passwordRejected);
        QVERIFY(QDir(out).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
        result = extract(archive, out, {QStringLiteral("данные/nested")}, secret, cancel);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(read(result.output + QStringLiteral("/данные/nested/секрет.txt")), QByteArray("private data"));
        QVERIFY(!QFileInfo::exists(result.output + QStringLiteral("/данные/other.txt")));
        result = extract(archive, out, {}, secret, cancel);
        QVERIFY2(result.ok(), qPrintable(result.error)); QCOMPARE(read(result.output + QStringLiteral("/данные/other.txt")), QByteArray("other"));
        // Independent decoder verifies that the created archive is interoperable.
        QProcess seven; seven.start("7z", {"t", archive}); QVERIFY(seven.waitForStarted());
        seven.write(secret.toUtf8() + '\n'); seven.closeWriteChannel(); QVERIFY(seven.waitForFinished(30000));
        QVERIFY2(seven.exitCode() == 0, seven.readAllStandardError().constData());
    }
    void encryptedRar_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::newRow("data") << "data.rar"; QTest::newRow("headers") << "header.rar";
    }
    void encryptedRar()
    {
        QFETCH(QString, fixture); QTemporaryDir root; std::atomic_bool cancel = false;
        const QString archive = QStringLiteral(ARCHIVE_FIXTURE_DIR) + '/' + fixture;
        auto result = list(archive, "12345678", cancel); QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.entries.size(), 2);
        result = extract(archive, root.path(), {}, {}, cancel);
        QVERIFY2(result.passwordRequired, qPrintable(result.error));
        result = extract(archive, root.path(), {}, "wrong", cancel);
        QVERIFY2(result.passwordRejected, qPrintable(result.error));
        QVERIFY(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
        result = extract(archive, root.path(), {}, "12345678", cancel);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(read(result.output + "/foo.txt").size(), 16); QCOMPARE(read(result.output + "/bar.txt").size(), 16);
    }
    void protectedCreationFailurePreservesOutput()
    {
        QTemporaryDir root; const QString source = root.filePath("file"), output = root.filePath("existing.7z");
        QVERIFY(write(source, "private")); QVERIFY(write(output, "original")); std::atomic_bool cancel = false;
        auto result = create({source}, output, Format::TarGz, cancel, {}, "secret");
        QVERIFY(!result.ok()); QCOMPARE(read(output), QByteArray("original"));
        result = create({source}, output, Format::SevenZip, cancel,
            [&cancel](qint64, qint64, const QString &) { cancel = true; }, "secret");
        QVERIFY(result.cancelled); QCOMPARE(read(output), QByteArray("original"));
        QCOMPARE(QDir(root.path()).entryList({".karyo-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
    }
    void fallbackRejectsUnsafeEntriesAndLinks()
    {
        QTemporaryDir root; std::atomic_bool cancel = false;
        const QString unsafe = root.filePath("unsafe.tar"); unsafeArchive(unsafe, "../escaped");
        auto result = SevenZip::extract(unsafe, root.path(), {}, {}, cancel, {});
        QVERIFY(!result.ok()); QVERIFY(!QFileInfo::exists(root.filePath("escaped")));
        QVERIFY(write(root.filePath("target"), "private"));
        QVERIFY(QFile::link(root.filePath("target"), root.filePath("link")));
        result = create({root.filePath("link")}, root.filePath("output.7z"), Format::SevenZip, cancel, {}, "secret");
        QVERIFY(!result.ok()); QVERIFY(!QFileInfo::exists(root.filePath("output.7z")));
        QProcess seven; seven.start("7z", {"a", "-snl", "-psecret", "-mhe=on", root.filePath("links.7z"), root.filePath("link")});
        QVERIFY(seven.waitForFinished()); QCOMPARE(seven.exitCode(), 0);
        const QString out = root.filePath("out"); QVERIFY(QDir().mkdir(out));
        result = extract(root.filePath("links.7z"), out, {}, "secret", cancel);
        QVERIFY(!result.ok()); QVERIFY(QDir(out).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void compressionLevels_data()
    {
        QTest::addColumn<int>("format"); QTest::addColumn<bool>("encrypted");
        QTest::newRow("zip") << int(Format::Zip) << false;
        QTest::newRow("tar-gz") << int(Format::TarGz) << false;
        QTest::newRow("7z") << int(Format::SevenZip) << false;
        QTest::newRow("zip-encrypted") << int(Format::Zip) << true;
        QTest::newRow("7z-encrypted") << int(Format::SevenZip) << true;
    }
    void compressionLevels()
    {
        QFETCH(int, format); QFETCH(bool, encrypted); QTemporaryDir root;
        const QByteArray payload(65536, 'A'); const QString source = root.filePath("source.txt");
        QVERIFY(write(source, payload)); std::atomic_bool cancel = false;
        const QString secret = encrypted ? QStringLiteral("fixture-password") : QString();
        qint64 storedSize = 0;
        for (int level : {0, 1, 5, 9}) {
            const QString output = root.filePath(QStringLiteral("level-%1.archive").arg(level));
            auto result = create({source}, output, Format(format), cancel, {}, secret, true, level);
            QVERIFY2(result.ok(), qPrintable(result.error));
            if (level == 0) storedSize = QFileInfo(output).size();
            else QVERIFY(QFileInfo(output).size() < storedSize);
            result = extract(output, root.path(), {}, secret, cancel);
            QVERIFY2(result.ok(), qPrintable(result.error)); QCOMPARE(read(result.output + "/source.txt"), payload);
        }
        auto result = create({source}, root.filePath("invalid"), Format(format), cancel, {}, secret, true, 10);
        QVERIFY(!result.ok()); QVERIFY(!QFileInfo::exists(root.filePath("invalid")));
    }
    void compressedStreamAndInvalidInput()
    {
        QTemporaryDir root;
        const QString source = root.filePath("reads.fastq");
        QVERIFY(write(source, "@read\nACGT\n+\nIIII\n"));
        QProcess gzip;
        gzip.setStandardOutputFile(root.filePath("reads.fastq.gz"));
        gzip.start("gzip", {"-c", source}); QVERIFY(gzip.waitForFinished()); QCOMPARE(gzip.exitCode(), 0);
        std::atomic_bool cancel = false;
        auto contents = list(root.filePath("reads.fastq.gz"), {}, cancel);
        QVERIFY2(contents.ok(), qPrintable(contents.error));
        QCOMPARE(contents.entries.size(), 1); QCOMPARE(contents.entries.first().path, QString("reads.fastq"));
        auto result = extract(root.filePath("reads.fastq.gz"), root.path(), {}, {}, cancel);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(read(result.output + "/reads.fastq"), read(source));
        QVERIFY(!list(source, {}, cancel).ok());
        QVERIFY(write(root.filePath("bad.zip"), "not an archive"));
        QVERIFY(!list(root.filePath("bad.zip"), {}, cancel).ok());
    }
    void largeStreamStaysCancellable()
    {
        QTemporaryDir root;
        const QString source = root.filePath("large.bin");
        QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray block(65536, '\x31');
        for (int i = 0; i < 2048; ++i) QCOMPARE(file.write(block), qint64(block.size()));
        file.close();
        std::atomic_bool cancel = false;
        const QString archive = root.filePath("large.tar.gz");
        auto result = create({source}, archive, Format::TarGz, cancel);
        QVERIFY2(result.ok(), qPrintable(result.error));
        auto contents = list(archive, {}, cancel);
        QVERIFY2(contents.ok(), qPrintable(contents.error));
        QCOMPARE(contents.entries.first().size, qint64(128) << 20);
        result = extract(archive, root.path(), {}, {}, cancel,
            [&cancel](qint64 bytes, qint64, const QString &) { if (bytes > 65536) cancel = true; });
        QVERIFY(result.cancelled);
        QVERIFY(!QFileInfo::exists(root.filePath("large")));
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.filePath("config").toUtf8());
    QCoreApplication app(argc, argv);
    ArchiveBackendTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "backendtest.moc"
