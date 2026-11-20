// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the buffer's own undo history and its cache: what a crash leaves
// behind comes back, and Ctrl+Z still walks through the edits.
//
//   karyo-editor-buffertest                      run the checks
//   karyo-editor-buffertest --crash DATA FILE    also leave a crashed
//       session in DATA (the editor's data folder) with unsaved edits to FILE
#include "buffer.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(bytes);
}

// Types text one character at a time, as keystrokes arrive.
void type(Buffer &buffer, const QString &text)
{
    QTextCursor cursor(buffer.document());
    cursor.movePosition(QTextCursor::End);
    for (const QChar c : text)
        cursor.insertText(QString(c));
}

} // namespace

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QTemporaryDir temp;
    const QString file = temp.filePath(QStringLiteral("Snakefile"));
    writeFile(file, "rule all:\n");

    {
        Buffer buffer(QStringLiteral("a"));
        QString error;
        check(buffer.load(file, &error), "load");
        check(!buffer.isModified() && !buffer.canUndo(), "a loaded file is unmodified");
        type(buffer, QStringLiteral("    input:"));
        buffer.breakTyping();
        QTextCursor cursor(buffer.document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral("\n        \"x\""));
        check(buffer.text() == QStringLiteral("rule all:\n    input:\n        \"x\""), "typed text");
        check(buffer.isModified(), "typing modifies");

        const QString cache = temp.filePath(QStringLiteral("cache-a"));
        check(buffer.writeCache(cache), "write cache");

        // What the next start finds after a crash.
        Buffer restored(QStringLiteral("a"));
        check(restored.readCache(cache, file, &error), "read cache");
        check(restored.text() == buffer.text(), "unsaved text restored");
        check(restored.isModified(), "restored text is unsaved");
        check(restored.undo() >= 0 && restored.text() == QStringLiteral("rule all:\n    input:"),
              "first undo takes the pasted line");
        // "    input:" was typed key by key: a step per word, the indentation first.
        check(restored.undo() >= 0 && restored.text() == QStringLiteral("rule all:\n    "), "undo takes a typed word");
        check(restored.undo() >= 0 && restored.text() == QStringLiteral("rule all:\n"), "undo takes the indentation");
        check(!restored.isModified(), "undone to the saved text");
        check(restored.redo() >= 0 && restored.redo() >= 0 && restored.redo() >= 0 && restored.text() == buffer.text(),
              "redo after restore");

        // Revert is a step too.
        check(restored.load(file, &error) && restored.text() == QStringLiteral("rule all:\n"), "revert");
        check(!restored.isModified(), "reverted is saved");
        check(restored.undo() >= 0 && restored.text() == buffer.text(), "undo brings back what revert dropped");

        // Replace all is one step.
        restored.beginGroup();
        QTextCursor edit(restored.document());
        edit = restored.document()->find(QStringLiteral("input"));
        edit.insertText(QStringLiteral("output"));
        edit = restored.document()->find(QStringLiteral("rule"));
        edit.insertText(QStringLiteral("checkpoint"));
        restored.endGroup();
        check(restored.text().startsWith(QStringLiteral("checkpoint all:\n    output:")), "grouped edits");
        check(restored.undo() >= 0 && restored.text() == buffer.text(), "one undo for a group");

        // Saving writes the text, and the history stays.
        check(restored.save(file, &error) && !restored.isModified(), "save");
        check(restored.canUndo(), "history kept after save");
    }

    {
        // A file changed while the editor was closed: the old history no
        // longer fits it and is dropped.
        Buffer buffer(QStringLiteral("b"));
        QString error;
        buffer.load(file, &error);
        type(buffer, QStringLiteral("# note"));
        buffer.save(file, &error);
        const QString cache = temp.filePath(QStringLiteral("cache-b"));
        buffer.writeCache(cache);
        writeFile(file, "rule other:\n");
        Buffer restored(QStringLiteral("b"));
        check(restored.readCache(cache, file, &error), "read cache of a changed file");
        check(restored.text() == QStringLiteral("rule other:\n") && !restored.canUndo(), "stale history dropped");
    }

    {
        const QString latinPath = temp.filePath(QStringLiteral("legacy-latin1.txt"));
        const QByteArray original("caf\xe9\n");
        writeFile(latinPath, original);
        Buffer buffer(QStringLiteral("latin1"));
        QString error;
        check(buffer.load(latinPath, &error), "load Latin-1 text");
        type(buffer, QString::fromUtf8("Привет"));
        check(!buffer.save(latinPath, &error), "refuse lossy Latin-1 save");
        QFile preserved(latinPath);
        preserved.open(QIODevice::ReadOnly);
        check(preserved.readAll() == original, "lossy save preserves original bytes");
        preserved.close();
        check(buffer.isModified(), "lossy save keeps unsaved state");
        check(buffer.needsUtf8(), "report encoding conversion needed");
        buffer.useUtf8();
        check(buffer.save(latinPath, &error), "save explicitly as UTF-8");
        check(buffer.encoding() == QLatin1String("UTF-8"), "conversion updates encoding");
        check(!buffer.isModified(), "successful conversion marks saved");
        QFile converted(latinPath);
        converted.open(QIODevice::ReadOnly);
        check(QString::fromUtf8(converted.readAll()) == QString::fromUtf8("café\nПривет"),
              "UTF-8 preserves old and new characters");
    }

    if (argc == 4 && qstrcmp(argv[1], "--crash") == 0) {
        // A session as a crash leaves it: unsaved edits, "clean" false.
        const QString data = QString::fromLocal8Bit(argv[2]);
        const QString target = QString::fromLocal8Bit(argv[3]);
        Buffer buffer(QStringLiteral("crashed"));
        QString error;
        buffer.load(target, &error);
        QTextCursor cursor(buffer.document());
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down);
        cursor.insertText(QStringLiteral("SAMPLES_DONE = True  # typed before the crash\n"));
        buffer.writeCache(data + QStringLiteral("/buffers/crashed"));
        QJsonObject tab{{QStringLiteral("id"), QStringLiteral("crashed")},
                        {QStringLiteral("path"), QFileInfo(target).absoluteFilePath()},
                        {QStringLiteral("cursor"), 30}};
        QJsonObject session{{QStringLiteral("version"), 1},
                            {QStringLiteral("clean"), false},
                            {QStringLiteral("current"), 0},
                            {QStringLiteral("tabs"), QJsonArray{tab}}};
        QDir().mkpath(data);
        writeFile(data + QStringLiteral("/session.json"), QJsonDocument(session).toJson());
    }

    std::fprintf(stderr, failures ? "buffer test: %d failed\n" : "buffer test: ok\n", failures);
    return failures ? 1 : 0;
}
