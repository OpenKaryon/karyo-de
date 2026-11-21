// SPDX-License-Identifier: GPL-3.0-or-later
// The pages of App Center: Discover (1i), a category, search results,
// updates, installed apps, an app's details and Environments (1j).
#pragma once

#include "packages.h"

#include <QWidget>

class CardGrid;
class QLabel;
class QVBoxLayout;

// A scrolling column of a page.
class Page : public QWidget
{
    Q_OBJECT
public:
    explicit Page(QWidget *parent = nullptr);
    QVBoxLayout *column() const { return m_column; }
    virtual void refresh() { }

Q_SIGNALS:
    void detailsRequested(const CatalogApp &app);
    // A link to another page of App Center ("See all", "Environments").
    void navigate(const QString &page);

protected:
    QVBoxLayout *m_column;
};

Page *createDiscoverPage(PackageManager *packages);
Page *createCategoryPage(
    PackageManager *packages, const QString &category, const QString &title, const QString &description);
Page *createUpdatesPage(PackageManager *packages);
Page *createInstalledPage(PackageManager *packages);
Page *createEnvironmentsPage(PackageManager *packages);

// Results for a query: the catalog, then Debian packages and Flathub apps.
class SearchPage : public Page
{
    Q_OBJECT
public:
    explicit SearchPage(PackageManager *packages, QWidget *parent = nullptr);
    void search(const QString &query);

private:
    void showRows(QVBoxLayout *layout, const QList<CatalogApp> &apps);

    PackageManager *m_packages;
    QString m_query;
    QLabel *m_title;
    CardGrid *m_catalog;
    QLabel *m_debianHeading;
    QVBoxLayout *m_debian;
    QLabel *m_flatpakHeading;
    CardGrid *m_flatpak;
};

class DetailsPage : public Page
{
    Q_OBJECT
public:
    DetailsPage(PackageManager *packages, const CatalogApp &app, QWidget *parent = nullptr);
};
