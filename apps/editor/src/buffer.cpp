// SPDX-License-Identifier: GPL-3.0-or-later
#include "buffer.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextDocumentLayout>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTextCursor>
#include <QTextDocument>

namespace {

constexpr quint32 Magic = 0x4b454448;  // "KEDH"
constexpr quint16 Version = 1;
// Typing within this many milliseconds of the last key joins its step.
constexpr qint64 TypingMs = 1500;
constexpr int MaxSteps = 5000;
constexpr qint64 MaxFileBytes = 64 * 1024 * 1024;

QString plain(QString text)
{
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

QByteArray checksum(const QString &text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1);
}

// Joins a keystroke into the previous edit: typing on, Backspace or Delete
// repeated. A new line or a word after a space starts a new step.
bool merge(Buffer::Edit &last, const Buffer::Edit &next)
{
    const bool lastInsert = last.removed.isEmpty() && !last.inserted.isEmpty();
    const bool lastDelete = last.inserted.isEmpty() && !last.removed.isEmpty();
    if (lastInsert && next.removed.isEmpty() && next.inserted.size() == 1) {
        const QChar c = next.inserted.at(0);
        const QChar previous = last.inserted.back();
        if (c == QLatin1Char('\n') || previous == QLatin1Char('\n')
            || (previous.isSpace() && !c.isSpace()))
            return false;
        if (last.position + last.inserted.size() != next.position)
            return false;
        last.inserted += next.inserted;
        return true;
    }
    if (lastDelete && next.inserted.isEmpty() && next.removed.size() == 1) {
        if (next.position + 1 == last.position) {  // Backspace
            last.removed.prepend(next.removed);
            last.position = next.position;
            return true;
        }
        if (next.position == last.position) {  // Delete
            last.removed += next.removed;
            return true;
        }
    }
    return false;
}

} // namespace

Buffer::Buffer(const QString &id, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_document(new QTextDocument(this))
{
    m_document->setDocumentLayout(new QPlainTextDocumentLayout(m_document));
    // This buffer keeps the history; Qt's would only duplicate it.
    m_document->setUndoRedoEnabled(false);
    connect(m_document, &QTextDocument::contentsChange, this, &Buffer::onContentsChange);
}

QString Buffer::title() const
{
    if (!m_path.isEmpty())
        return QFileInfo(m_path).fileName();
    return m_untitled > 1 ? tr("Untitled %1").arg(m_untitled) : tr("Untitled");
}

void Buffer::onContentsChange(int position, int removed, int added)
{
    // Counts may include the document's final paragraph separator.
    const int length = m_document->characterCount() - 1;
    const int addedChars = qBound(0, added, length - position);
    const int removedChars = qBound(0, removed, int(m_shadow.size()) - position);
    QTextCursor cursor(m_document);
    cursor.setPosition(position);
    cursor.setPosition(position + addedChars, QTextCursor::KeepAnchor);
    const QString inserted = plain(cursor.selectedText());
    const QString gone = m_shadow.mid(position, removedChars);
    // Highlighting reports format changes as replacing text by itself.
    if (gone == inserted)
        return;
    m_shadow.replace(position, removedChars, inserted);
    if (m_shadow.size() != length) {
        // Out of step with the document: start over rather than keep a
        // history that no longer fits the text.
        m_shadow = plain(m_document->toPlainText());
        m_steps.clear();
        m_index = 0;
        m_savedIndex = -1;
        Q_EMIT modifiedChanged(true);
        Q_EMIT dirtied();
        return;
    }
    if (m_recording)
        record({position, gone, inserted});
    Q_EMIT dirtied();
}

void Buffer::record(const Edit &edit)
{
    const bool wasModified = isModified();
    if (m_index < m_steps.size()) {
        // A new edit after Undo: the undone steps are gone.
        m_steps.erase(m_steps.begin() + m_index, m_steps.end());
        if (m_savedIndex > m_index)
            m_savedIndex = -1;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_group > 0 && m_groupOpen) {
        m_steps.last().edits.append(edit);
    } else if (m_group == 0 && !m_breakTyping && !m_steps.isEmpty() && m_index != m_savedIndex
               && now - m_steps.last().time < TypingMs && merge(m_steps.last().edits.last(), edit)) {
        m_steps.last().time = now;
    } else {
        m_steps.append({{edit}, now});
        m_index = int(m_steps.size());
        m_groupOpen = m_group > 0;
    }
    m_breakTyping = false;
    if (m_steps.size() > MaxSteps) {
        const int drop = int(m_steps.size()) - MaxSteps;
        m_steps.remove(0, drop);
        m_index -= drop;
        m_savedIndex = m_savedIndex >= drop ? m_savedIndex - drop : -1;
    }
    if (wasModified != isModified())
        Q_EMIT modifiedChanged(isModified());
}

void Buffer::apply(const Edit &edit, bool forward)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(edit.position);
    cursor.setPosition(edit.position + int((forward ? edit.removed : edit.inserted).size()),
                       QTextCursor::KeepAnchor);
    cursor.insertText(forward ? edit.inserted : edit.removed);
}

void Buffer::setIndex(int index)
{
    const bool wasModified = isModified();
    m_index = index;
    m_breakTyping = true;
    if (wasModified != isModified())
        Q_EMIT modifiedChanged(isModified());
    Q_EMIT dirtied();
}

int Buffer::undo()
{
    if (!canUndo())
        return -1;
    const Step step = m_steps.at(m_index - 1);
    int cursor = -1;
    m_recording = false;
    for (qsizetype i = step.edits.size() - 1; i >= 0; --i) {
        const Edit &edit = step.edits.at(i);
        apply(edit, false);
        cursor = edit.position + int(edit.removed.size());
    }
    m_recording = true;
    setIndex(m_index - 1);
    return cursor;
}

int Buffer::redo()
{
    if (!canRedo())
        return -1;
    const Step step = m_steps.at(m_index);
    int cursor = -1;
    m_recording = false;
    for (const Edit &edit : step.edits) {
        apply(edit, true);
        cursor = edit.position + int(edit.inserted.size());
    }
    m_recording = true;
    setIndex(m_index + 1);
    return cursor;
}

void Buffer::beginGroup()
{
    if (m_group++ == 0)
        m_groupOpen = false;
}

void Buffer::endGroup()
{
    if (m_group > 0 && --m_group == 0) {
        m_groupOpen = false;
        m_breakTyping = true;
    }
}

void Buffer::replaceAll(const QString &text, bool asStep)
{
    if (asStep) {
        beginGroup();
        QTextCursor cursor(m_document);
        cursor.select(QTextCursor::Document);
        cursor.insertText(text);
        endGroup();
        return;
    }
    m_recording = false;
    m_document->setPlainText(text);
    m_recording = true;
    m_steps.clear();
    m_index = 0;
    m_savedIndex = 0;
}

bool Buffer::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    if (file.size() > MaxFileBytes) {
        *error = tr("The file is larger than 64 MB.");
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.left(8192).contains('\0')) {
        *error = tr("This looks like a binary file.");
        return false;
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    m_latin1 = utf8.hasError();
    if (m_latin1)
        text = QString::fromLatin1(bytes);
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    m_crlf = text.contains(QLatin1String("\r\n"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));

    m_path = QFileInfo(path).absoluteFilePath();
    // Reloading over text is a step of its own, so Ctrl+Z brings it back.
    const bool wasModified = isModified();
    replaceAll(text, !m_shadow.isEmpty() || !m_steps.isEmpty());
    m_savedIndex = m_index;
    readStamp();
    if (wasModified)
        Q_EMIT modifiedChanged(false);
    Q_EMIT dirtied();
    return true;
}

QByteArray Buffer::encode(const QString &text) const
{
    QString out = text;
    if (m_crlf)
        out.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    return m_latin1 ? out.toLatin1() : out.toUtf8();
}

bool Buffer::needsUtf8() const
{
    if (!m_latin1)
        return false;
    for (const QChar c : m_shadow) {
        if (c.unicode() > 0xff)
            return true;
    }
    return false;
}

void Buffer::useUtf8()
{
    if (!m_latin1)
        return;
    const bool wasModified = isModified();
    m_latin1 = false;
    m_savedIndex = -1;
    if (!wasModified)
        Q_EMIT modifiedChanged(true);
    Q_EMIT dirtied();
}

bool Buffer::save(const QString &path, QString *error)
{
    if (needsUtf8()) {
        *error = tr("Some characters cannot be saved as ISO-8859-1. Choose UTF-8 to keep all text.");
        return false;
    }
    const QByteArray bytes = encode(m_shadow);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    const bool wasModified = isModified();
    m_savedIndex = m_index;
    m_breakTyping = true;
    readStamp();
    if (wasModified)
        Q_EMIT modifiedChanged(false);
    Q_EMIT dirtied();
    return true;
}

void Buffer::readStamp()
{
    const QFileInfo info(m_path);
    m_diskTime = info.exists() ? info.lastModified() : QDateTime();
    m_diskSize = info.exists() ? info.size() : -1;
}

void Buffer::acceptDiskVersion() { readStamp(); }

bool Buffer::deletedOnDisk() const { return !m_path.isEmpty() && m_diskSize >= 0 && !QFileInfo::exists(m_path); }

void Buffer::setNewPath(const QString &path)
{
    m_path = QFileInfo(path).absoluteFilePath();
    m_diskTime = QDateTime();
    m_diskSize = -1;
}

bool Buffer::existsOnDisk() const { return !m_path.isEmpty() && QFileInfo::exists(m_path); }

bool Buffer::changedOnDisk() const
{
    if (m_path.isEmpty() || m_diskSize < 0)
        return false;
    const QFileInfo info(m_path);
    return info.exists() && (info.lastModified() != m_diskTime || info.size() != m_diskSize);
}

void Buffer::setCrlf(bool crlf)
{
    if (crlf == m_crlf)
        return;
    m_crlf = crlf;
    // The text on disk no longer matches.
    const bool wasModified = isModified();
    m_savedIndex = -1;
    if (!wasModified)
        Q_EMIT modifiedChanged(true);
    Q_EMIT dirtied();
}

bool Buffer::writeCache(const QString &dir) const
{
    if (!QDir().mkpath(dir))
        return false;
    // The text itself only while it differs from the file.
    const QString textPath = dir + QStringLiteral("/text.txt");
    if (isModified() || m_path.isEmpty()) {
        QSaveFile text(textPath);
        if (!text.open(QIODevice::WriteOnly) || text.write(m_shadow.toUtf8()) < 0 || !text.commit())
            return false;
    } else {
        QFile::remove(textPath);
    }
    QSaveFile file(dir + QStringLiteral("/history.bin"));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out << Magic << Version << checksum(m_shadow) << qint32(m_index) << qint32(m_savedIndex) << m_crlf
        << m_latin1 << m_diskTime << m_diskSize << qint32(m_steps.size());
    for (const Step &step : m_steps) {
        out << step.time << qint32(step.edits.size());
        for (const Edit &edit : step.edits)
            out << qint32(edit.position) << edit.removed << edit.inserted;
    }
    return file.commit();
}

bool Buffer::readCache(const QString &dir, const QString &path, QString *error)
{
    QFile textFile(dir + QStringLiteral("/text.txt"));
    const bool unsaved = textFile.open(QIODevice::ReadOnly);
    if (unsaved) {
        m_path = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
        replaceAll(QString::fromUtf8(textFile.readAll()), false);
        m_savedIndex = -1;
        readStamp();
    } else if (!path.isEmpty()) {
        if (!load(path, error))
            return false;
    } else {
        // An untitled text that was empty.
        replaceAll(QString(), false);
    }

    QFile file(dir + QStringLiteral("/history.bin"));
    if (file.open(QIODevice::ReadOnly)) {
        QDataStream in(&file);
        quint32 magic = 0;
        quint16 version = 0;
        QByteArray sum;
        in >> magic >> version >> sum;
        // A history fits only the text it was written with; a file changed
        // while the editor was closed starts a fresh one.
        if (magic == Magic && version == Version && sum == checksum(m_shadow)) {
            qint32 index = 0, savedIndex = 0, count = 0;
            bool crlf = false, latin1 = false;
            QDateTime diskTime;
            qint64 diskSize = -1;
            in >> index >> savedIndex >> crlf >> latin1 >> diskTime >> diskSize >> count;
            QList<Step> steps;
            for (qint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
                Step step;
                qint32 edits = 0;
                in >> step.time >> edits;
                for (qint32 j = 0; j < edits && in.status() == QDataStream::Ok; ++j) {
                    Edit edit;
                    qint32 position = 0;
                    in >> position >> edit.removed >> edit.inserted;
                    edit.position = position;
                    step.edits.append(edit);
                }
                steps.append(step);
            }
            if (in.status() == QDataStream::Ok && index >= 0 && index <= steps.size()) {
                m_steps = steps;
                m_index = index;
                m_crlf = crlf;
                m_latin1 = latin1;
                if (unsaved) {
                    m_savedIndex = savedIndex;
                    m_diskTime = diskTime;
                    m_diskSize = diskSize;
                } else {
                    // The file on disk is this text.
                    m_savedIndex = index;
                }
            }
        }
    }
    m_breakTyping = true;
    Q_EMIT modifiedChanged(isModified());
    return true;
}
