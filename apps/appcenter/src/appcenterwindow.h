// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "packages.h"
#include <QList>
#include <QPointer>
#include <karyo/ui/window.h>
class QLabel;
class QStackedWidget;
class QTimer;
class Page;
class SearchPage;
namespace Karyo::Ui
{
class SearchField;
class NavList;
}
class AppCenterWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    AppCenterWindow();
    void showSection(const QString &key);
    void showApp(const QString &id);

private:
    enum class View
    {
        Section,
        Search,
        Details
    };
    void build();
    void showIndex(int index);
    void openDetails(const CatalogApp &app);
    void search();
    void back();
    void clearExtra();
    void message(const QString &text);
    void updateBadge();
    PackageManager *m_packages;
    Karyo::Ui::SearchField *m_search;
    Karyo::Ui::NavList *m_nav = nullptr;
    Karyo::Ui::IconButton *m_back;
    QLabel *m_title, *m_message = nullptr;
    QTimer *m_debounce;
    QStackedWidget *m_stack = nullptr;
    QList<Page *> m_pages;
    SearchPage *m_searchPage = nullptr;
    QWidget *m_searchScroll = nullptr;
    QPointer<QWidget> m_extra;
    const QStringList m_keys { "discover", "bio", "apps", "cli", "environments", "updates", "installed" };
    int m_current = 0;
    View m_view = View::Section, m_beforeDetails = View::Section;
    CatalogApp m_detail;
    QString m_notice;
};
