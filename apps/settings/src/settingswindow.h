// SPDX-License-Identifier: GPL-3.0-or-later
// The Settings window (screen 1h): a back arrow and "Settings" in the title
// strip; the user card, "Find a setting" and the sections at the left; the
// section's page at the right. Pages are made when first shown, and made
// again when the theme changes so every color follows it.
#pragma once

#include <karyo/ui/window.h>

#include <QList>
#include <QPointer>

#include <functional>

class QLabel;
class QStackedWidget;
class SettingsPage;

namespace Karyo::Ui {
class IconButton;
class NavList;
class SearchField;
}

class SettingsWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    SettingsWindow();
    // Opens a section by its key ("personalization", "about"...).
    void showSection(const QString &key);

private:
    struct Section
    {
        QString key;
        QString glyph;
        QString name;
        std::function<SettingsPage *(QWidget *)> create;
    };

    void build();
    void showIndex(int index);
    void openSubPage(SettingsPage *page);
    void back();
    void find(const QString &text);
    SettingsPage *page(int index);
    QWidget *wrap(SettingsPage *page);

    QList<Section> m_sections;
    QList<QPointer<SettingsPage>> m_pages;
    QList<QWidget *> m_subPages;
    Karyo::Ui::IconButton *m_back;
    QLabel *m_title;
    Karyo::Ui::NavList *m_nav = nullptr;
    Karyo::Ui::SearchField *m_find = nullptr;
    QStackedWidget *m_stack = nullptr;
    int m_current = 0;
    int m_updates = -1;
};
