// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileops.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <filesystem>
#include <sys/stat.h>

class FileOperationsTest : public QObject {
    Q_OBJECT
    static void put(const QString &path, const QByteArray &value = "data") {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(value), value.size());
    }
    static QStringList run(FileJob::Kind kind, const QStringList &paths, const QString &target = {}) {
        QSignalSpy finished(FileJob::start(kind, paths, target), &FileJob::finished);
        if (!finished.wait(15000)) {
            QTest::qFail("File job did not finish", __FILE__, __LINE__);
            return {"timeout"};
        }
        return finished.first().first().toStringList();
    }
private Q_SLOTS:
    void clipboardFormats() {
        FileClipboard::set({"/tmp/a b", "/tmp/other"}, true);
        bool cut = false;
        QCOMPARE(FileClipboard::paths(&cut), QStringList({"/tmp/a b", "/tmp/other"}));
        QVERIFY(cut);
        QVERIFY(FileClipboard::hasFiles());
        QCOMPARE(QApplication::clipboard()->mimeData()->data("application/x-kde-cutselection"), QByteArray("1"));
        auto *data = new QMimeData;
        data->setUrls({QUrl("https://example.invalid/file")});
        QApplication::clipboard()->setMimeData(data);
        QVERIFY(!FileClipboard::hasFiles());
        QApplication::clipboard()->clear();
        cut = true;
        QVERIFY(FileClipboard::paths(&cut).isEmpty());
        QVERIFY(!cut);
    }
    void copyCollisionAndLinks() {
        QTemporaryDir dir;
        const QString from = dir.filePath("source"), dest = dir.filePath("dest");
        QVERIFY(QDir().mkpath(from)); QVERIFY(QDir().mkpath(dest));
        put(from + "/part.tar.gz", "new"); put(dest + "/part.tar.gz", "old");
        QVERIFY(run(FileJob::Kind::Copy, {from + "/part.tar.gz"}, dest).isEmpty());
        QFile old(dest + "/part.tar.gz"); QVERIFY(old.open(QIODevice::ReadOnly)); QCOMPARE(old.readAll(), QByteArray("old"));
        QFile copy(dest + "/part (2).tar.gz"); QVERIFY(copy.open(QIODevice::ReadOnly)); QCOMPARE(copy.readAll(), QByteArray("new"));
        std::filesystem::create_symlink("missing", QFile::encodeName(dest + "/dangling").constData());
        QCOMPARE(FileJob::freeName(dest, "dangling"), QString("dangling (2)"));
        std::filesystem::create_symlink("part.tar.gz", QFile::encodeName(from + "/relative").constData());
        QVERIFY(run(FileJob::Kind::Copy, {from}, dest).isEmpty());
        QCOMPARE(std::filesystem::read_symlink(QFile::encodeName(dest + "/source/relative").constData()).string(), std::string("part.tar.gz"));
        QVERIFY(QFileInfo(dest + "/source/relative").exists());
    }
    void rejectDescendantAndFailedTree() {
        QTemporaryDir dir;
        const QString source = dir.filePath("source"), child = source + "/child", dest = dir.filePath("dest");
        QVERIFY(QDir().mkpath(child)); QVERIFY(QDir().mkpath(dest));
        put(source + "/keep");
        const QString alias = dir.filePath("alias");
        QVERIFY(QFile::link(child, alias));
        QVERIFY(!run(FileJob::Kind::Copy, {source}, alias).isEmpty());
        QVERIFY(!QFileInfo::exists(child + "/source"));
        const QByteArray fifo = QFile::encodeName(source + "/pipe");
        QCOMPARE(::mkfifo(fifo.constData(), 0600), 0);
        QVERIFY(!run(FileJob::Kind::Copy, {source}, dest).isEmpty());
        QVERIFY(!QFileInfo::exists(dest + "/source"));
        QVERIFY(QFileInfo::exists(source + "/keep"));
        QVERIFY(QDir(dest).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }
    void movesAndDelete() {
        QTemporaryDir dir;
        const QString dest = dir.filePath("dest"); QVERIFY(QDir().mkpath(dest));
        const QString file = dir.filePath("original"); put(file);
        QVERIFY(run(FileJob::Kind::Move, {file}, dest).isEmpty());
        QVERIFY(!QFileInfo::exists(file)); QVERIFY(QFileInfo::exists(dest + "/original"));
        QVERIFY(run(FileJob::Kind::Move, {dest + "/original"}, dest).isEmpty());
        QVERIFY(!QFileInfo::exists(dest + "/original (2)"));
        QVERIFY(run(FileJob::Kind::Delete, {dest + "/original"}).isEmpty());
        QVERIFY(!QFileInfo::exists(dest + "/original"));
        QVERIFY(!run(FileJob::Kind::Copy, {file}, dir.filePath("missing")).isEmpty());
        QVERIFY(run(FileJob::Kind::Copy, {}, dest).isEmpty());
    }
};
QTEST_MAIN(FileOperationsTest)
#include "fileoperationstest.moc"
