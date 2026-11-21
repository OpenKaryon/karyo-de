// SPDX-License-Identifier: GPL-3.0-or-later
// Karyo PDF Reader: a tab per document in the title strip, a toolbar with
// the sidebar, page, zoom and search controls, and the open document under
// it. Without documents it offers to open one and lists the recent ones.
#pragma once

#include <karyo/ui/window.h>

class DocumentPage;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QVBoxLayout;

namespace Karyo::Ui {
class IconButton;
class PillButton;
class SearchField;
}

class ReaderWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit ReaderWindow(QWidget *parent = nullptr);

    // Opens files in tabs, or shows them when they are open already.
    void openFiles(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    DocumentPage *current() const;
    void openDialog();
    void closeTab(int index);
    void showPage(int index);
    void showSidebarMenu();
    void showZoomMenu();
    void findNext(bool backward);

    void updateToolbar();
    void updatePage();
    void updateZoom();
    void updateMatches();
    void updateRecent();
    void remember(const QString &path);

    Karyo::Ui::TitleTabs *m_tabs;
    QStackedWidget *m_stack;
    QList<DocumentPage *> m_pages;

    QWidget *m_toolbar;
    Karyo::Ui::PillButton *m_sidebar;
    QLineEdit *m_pageEdit;
    QLabel *m_pageCount;
    Karyo::Ui::PillButton *m_zoom;
    Karyo::Ui::SearchField *m_find;
    QLabel *m_matches;

    QVBoxLayout *m_recent;
    QWidget *m_recentBox;
};

// org.openkaryon.Reader on the session bus: a second start hands its files
// to the running window, which opens them in tabs.
class ReaderService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.openkaryon.Reader")
public:
    explicit ReaderService(ReaderWindow *window);

public Q_SLOTS:
    Q_SCRIPTABLE void Open(const QStringList &paths);

private:
    ReaderWindow *m_window;
};
