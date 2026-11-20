// SPDX-License-Identifier: GPL-3.0-or-later
// One open file or untitled text: the document, how it is stored on disk,
// and an undo history of its own. Qt's undo stack cannot be saved, so edits
// are recorded from QTextDocument::contentsChange instead; the history and
// unsaved text go to the editor's cache and survive restarts and crashes.
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

class QTextDocument;

class Buffer : public QObject
{
    Q_OBJECT
public:
    // One change: the text at position that was removed and what replaced it.
    struct Edit
    {
        int position = 0;
        QString removed;
        QString inserted;
    };
    // What one Ctrl+Z takes back: typing a word, a paste, Replace all.
    struct Step
    {
        QList<Edit> edits;
        qint64 time = 0;
    };

    explicit Buffer(const QString &id, QObject *parent = nullptr);

    QString id() const { return m_id; }
    QTextDocument *document() const { return m_document; }
    QString path() const { return m_path; }
    // "Untitled 2" for new texts, the file name otherwise.
    QString title() const;
    void setUntitledNumber(int number) { m_untitled = number; }
    int untitledNumber() const { return m_untitled; }
    QString text() const { return m_shadow; }

    // Reads a file into the buffer as an undoable step when the buffer had
    // text (Revert, Reload), or as the start of the history otherwise.
    bool load(const QString &path, QString *error);
    bool save(const QString &path, QString *error);
    // The file changed on disk since it was loaded or saved.
    bool changedOnDisk() const;
    bool existsOnDisk() const;
    // The file was there when loaded or saved and is gone now.
    bool deletedOnDisk() const;
    // A file that does not exist yet ("karyo-editor notes.md"): saving creates it.
    void setNewPath(const QString &path);
    // Keep this text over the changed file: stop reporting the change.
    void acceptDiskVersion();

    bool isModified() const { return m_index != m_savedIndex; }
    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_steps.size(); }
    // Return where the cursor goes, or -1 when there was nothing to do.
    int undo();
    int redo();
    // Edits between begin and end are one step (Replace all, indenting lines).
    void beginGroup();
    void endGroup();
    // The cursor moved: typing after this starts a new step.
    void breakTyping() { m_breakTyping = true; }

    QString encoding() const { return m_latin1 ? QStringLiteral("ISO-8859-1") : QStringLiteral("UTF-8"); }
    // Refuse a lossy legacy save; the UI can explicitly offer UTF-8 instead.
    bool needsUtf8() const;
    void useUtf8();
    bool crlf() const { return m_crlf; }
    void setCrlf(bool crlf);

    // The cache: unsaved text and the history, in dir.
    bool writeCache(const QString &dir) const;
    // Restores what writeCache() kept; the file on disk is read when no
    // unsaved text was kept. A history that no longer fits the text is dropped.
    bool readCache(const QString &dir, const QString &path, QString *error);

Q_SIGNALS:
    void modifiedChanged(bool modified);
    // Text or history changed: the cache needs writing.
    void dirtied();

private:
    void onContentsChange(int position, int removed, int added);
    void record(const Edit &edit);
    void apply(const Edit &edit, bool forward);
    void setIndex(int index);
    void replaceAll(const QString &text, bool asStep);
    void readStamp();
    QByteArray encode(const QString &text) const;

    QString m_id;
    QTextDocument *m_document;
    QString m_path;
    int m_untitled = 0;
    // The text as of the last change, to know what an edit removed.
    QString m_shadow;
    QList<Step> m_steps;
    int m_index = 0;        // steps applied
    int m_savedIndex = 0;   // m_index when the text matched the file; -1 never
    bool m_recording = true;
    int m_group = 0;
    bool m_groupOpen = false;
    bool m_breakTyping = false;
    bool m_crlf = false;
    bool m_latin1 = false;
    QDateTime m_diskTime;
    qint64 m_diskSize = -1;
};
