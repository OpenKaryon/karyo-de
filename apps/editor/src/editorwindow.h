// SPDX-License-Identifier: GPL-3.0-or-later
// Karyo Text Editor (design 1g): one window, a tab per file in the title
// strip, a menu row, the code card and a status row. Like Notepad++ it keeps
// its session: closing the window keeps every tab, unsaved text and its undo
// history in ~/.local/share/openkaryon/openkaryon-editor, and the next start
// (or the first one after a crash) brings them back.
#pragma once

#include <karyo/ui/window.h>

#include <QSet>

class Buffer;
class CodeEditor;
class EditorPage;
class QLabel;
class QMenu;
class QStackedWidget;
class QTimer;
enum class Language;

namespace Karyo::Ui {
class PillButton;
class SearchField;
class IconButton;
}

class EditorWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit EditorWindow(QWidget *parent = nullptr);

    // Reopens the last session's tabs; false when there were none.
    bool restoreSession();
    // Opens files in tabs, or shows them when they are open already.
    void openFiles(const QStringList &paths);
    void newUntitled();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    bool m_recoveryWarning = false;
    EditorPage *current() const;
    EditorPage *pageFor(Buffer *buffer) const;
    int addPage(Buffer *buffer, Language language, bool languageChosen);
    void showPage(int index);
    bool closeTab(int index);
    bool prepareEncoding(EditorPage *page);
    bool save(EditorPage *page);
    bool saveAs(EditorPage *page);
    void saveAll();
    void revert(EditorPage *page);
    void checkDisk(EditorPage *page);
    void setLanguage(EditorPage *page, Language language);

    void buildMenus();
    QMenu *addMenu(const QString &title);
    void showFind(bool replace);
    bool findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void countMatches();
    void goToLine();
    // The Run button's text and command (%1: the quoted file name).
    QPair<QString, QString> runner(Language language) const;
    void run();
    void zoom(int steps);

    void updateTab(EditorPage *page);
    void updateStatus();
    void updateRun();

    QString dataDir() const;
    QString cacheDir(Buffer *buffer) const;
    void scheduleCache(Buffer *buffer);
    void writeCaches();
    void writeSession(bool clean);

    Karyo::Ui::TitleTabs *m_tabs;
    QStackedWidget *m_stack;
    QList<EditorPage *> m_pages;

    QList<QPair<Karyo::Ui::PillButton *, QMenu *>> m_menus;
    Karyo::Ui::PillButton *m_run;
    QWidget *m_findBar;
    Karyo::Ui::SearchField *m_find;
    Karyo::Ui::SearchField *m_replace;
    QWidget *m_replaceButtons;
    Karyo::Ui::IconButton *m_matchCase;
    QLabel *m_findStatus;

    QLabel *m_position;
    QAbstractButton *m_indent;
    QLabel *m_encoding;
    QAbstractButton *m_lineEnding;
    QLabel *m_environment;
    QAbstractButton *m_language;

    QTimer *m_cacheTimer;
    QSet<Buffer *> m_dirty;
    bool m_wrap = false;
    qreal m_fontPixels = 14.5;
};

// org.openkaryon.Editor on the session bus: a second start hands its files
// to the running window, which opens them in tabs.
class EditorService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.openkaryon.Editor")
public:
    explicit EditorService(EditorWindow *window);

public Q_SLOTS:
    Q_SCRIPTABLE void Open(const QStringList &paths);

private:
    EditorWindow *m_window;
};
