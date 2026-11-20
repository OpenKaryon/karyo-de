// SPDX-License-Identifier: GPL-3.0-or-later
// Start (KaryonStart.dc.html): a glass card centered above the taskbar with
// the search field, then the pinned apps and recent files, all apps by
// category, or search-first results with a preview; the user and power
// buttons sit in the footer. Typing anywhere switches to search.
#pragma once

#include "appindex.h"
#include "flyout.h"

#include <QHash>
#include <QList>

#include <functional>

class AppList;
class ChipButton;
class LinkButton;
class PinnedGrid;
class PreviewCard;
class QLabel;
class QScreen;
class QStackedWidget;
class RecentList;
class ResultList;
struct SearchResult;

namespace Karyo::Ui {
class NavList;
class SearchField;
}

class StartMenu : public Flyout
{
    Q_OBJECT
public:
    // Pin to taskbar in an app's menu: whether the taskbar has it, and how to change that.
    struct TaskbarPins
    {
        std::function<bool(const QString &)> isPinned;
        std::function<void(const QString &, bool)> setPinned;
    };

    StartMenu(AppIndex *index, QScreen *screen, TaskbarPins taskbarPins);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Page { Home, AllApps, Search };

    QWidget *buildHome();
    QWidget *buildAllApps();
    QWidget *buildSearch();
    QWidget *buildFooter();
    QLabel *heading(const QString &text, QWidget *parent) const;

    void showPage(Page page);
    void refreshHome();
    void showCategory(int row);
    void runSearch();
    void activate(const SearchResult &result);
    void showAppMenu(const AppInfo &app, const QPoint &globalPos);
    void showPowerMenu(QWidget *anchor);
    QRect cardRect() const;

    AppIndex *m_index;
    TaskbarPins m_taskbarPins;
    Karyo::Ui::SearchField *m_search = nullptr;
    QStackedWidget *m_pages = nullptr;
    Page m_page = Page::Home;
    Page m_browsePage = Page::Home;  // where clearing the search returns

    PinnedGrid *m_pinned = nullptr;
    RecentList *m_recent = nullptr;
    QLabel *m_allAppsCount = nullptr;
    Karyo::Ui::NavList *m_categories = nullptr;
    // What each row of m_categories lists; headings list nothing.
    struct CategoryRow
    {
        QString key;
        bool installed = true;
    };
    QHash<int, CategoryRow> m_categoryRows;
    AppList *m_appList = nullptr;
    QList<ChipButton *> m_chips;
    ResultList *m_results = nullptr;
    PreviewCard *m_preview = nullptr;
    QList<FileHit> m_fileHits;
    QString m_fileQuery;
};
