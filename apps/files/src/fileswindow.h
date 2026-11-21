// SPDX-License-Identifier: GPL-3.0-or-later
// Karyo Files (design 1e): Explorer-style window with tabs, breadcrumb,
// command bar, Places, the folder view and the details pane.
#pragma once

#include <karyo/ui/desktopentry.h>
#include <karyo/ui/window.h>

#include <QDeadlineTimer>
#include <QHash>
#include <QStringList>
#include <QTimer>

class ArchiveJob;
class Breadcrumb;
class DetailsPane;
class FolderView;
class Places;
class QFileInfo;
class QLabel;
class QMenu;
class QStackedWidget;
class Toplevel;
class ToplevelManager;

class FilesWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit FilesWindow(QWidget *parent = nullptr);
    // select: a file in the folder to select once it is listed.
    void openTab(const QString &path, const QString &select = QString());
    // Reuse a matching tab, preserving its history, view and unrelated tabs.
    bool revealFolder(const QString &path, const QStringList &items = {});

private:
    FolderView *current() const;
    void closeTab(int index);
    void syncChrome();
    void setDetailsVisible(bool visible);
    void updateStatus();
    void openFile(const QString &path);
    bool launchFile(const DesktopEntry &app, const QString &path);
    void windowOpened(Toplevel *window, bool added);
    void updateOpeningCursor();
    void showOpenWith(const QString &path, const QPoint &globalPos);
    void showContextMenu(const QPoint &globalPos, bool onItem);
    void showNewMenu();
    void showSortMenu();
    void showViewMenu();
    void newFolder();
    void newTextFile();
    void copySelection(bool cut);
    void paste();
    void trashSelection();
    void deleteSelection();
    void restoreFromTrash();
    void emptyTrash();
    void openTerminal(const QString &directory);
    void runJob(int kind, const QStringList &sources, const QString &target);
    void addArchiveActions(QMenu *menu, const QList<QFileInfo> &selected);
    void runArchive(ArchiveJob *job, const QString &verb);
    bool inTrash() const;

    Karyo::Ui::TitleTabs *m_tabs;
    QStackedWidget *m_stack;
    Breadcrumb *m_breadcrumb;
    Karyo::Ui::SearchField *m_search;
    Places *m_places;
    DetailsPane *m_details;
    Karyo::Ui::IconButton *m_back;
    Karyo::Ui::IconButton *m_forward;
    Karyo::Ui::IconButton *m_up;
    Karyo::Ui::IconButton *m_paste;
    Karyo::Ui::PillButton *m_detailsToggle;
    QWidget *m_trashBar;
    QLabel *m_status;
    QLabel *m_free;
    QLabel *m_progress;

    struct PendingOpen {
        DesktopEntry app;
        QString windowsClass;
        qint64 pid;
        quint64 lastActivation;
        QDeadlineTimer deadline;
    };
    QHash<QPair<QString, QString>, PendingOpen> m_opening;
    QTimer m_openingTimer;
    ToplevelManager *m_openingWindows = nullptr;
};
