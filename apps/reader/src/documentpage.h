// SPDX-License-Identifier: GPL-3.0-or-later
// One open PDF: the pages in a continuous view, a sidebar with page
// thumbnails or the document's contents, and search with every match
// highlighted. It remembers where the reader stopped in each file.
#pragma once

#include <QWidget>

class QPdfBookmarkModel;
class QPdfDocument;
class QPdfSearchModel;
class QPdfView;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QTreeView;
class Thumbnails;

class DocumentPage : public QWidget
{
    Q_OBJECT
public:
    explicit DocumentPage(QWidget *parent = nullptr);
    ~DocumentPage() override;

    // False with error() when the file cannot be opened; a password is asked for.
    bool open(const QString &path);
    QString path() const { return m_path; }
    QString title() const;
    QString error() const { return m_error; }

    int pageCount() const;
    int currentPage() const;
    void goToPage(int page);

    // Fit width, fit page, or a factor (1.0 is 100 %). zoom() is the factor
    // the pages are shown at, in the fit modes too.
    enum class Fit { Width, Page, Custom };
    void setFit(Fit fit);
    void zoomTo(qreal factor);
    void zoomBy(qreal factor);
    qreal zoom() const;
    Fit fit() const { return m_fit; }

    void setSidebar(int mode);  // 0 hidden, 1 pages, 2 contents
    int sidebar() const { return m_sidebarMode; }
    bool hasContents() const;

    void search(const QString &text);
    int matchCount() const;  // -1 while the reader is still typing
    int currentMatch() const { return m_match; }
    void nextMatch(bool backward);

    // Remembers the page and zoom for this file.
    void savePosition() const;

Q_SIGNALS:
    void pageChanged();
    void zoomChanged();
    void matchesChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void restorePosition();
    void showMatch();
    void runSearch();
    void showPageMenu(const QPoint &globalPos);

    QString m_path;
    QString m_error;
    QPdfDocument *m_document;
    QPdfView *m_view;
    QPdfSearchModel *m_search;
    QTimer *m_searchTimer;
    QString m_searchText;
    QPdfBookmarkModel *m_bookmarks;
    QWidget *m_sidebar;
    QStackedWidget *m_sidebarPages;
    Thumbnails *m_thumbnails;
    QScrollArea *m_thumbScroll;
    QTreeView *m_contents;
    Fit m_fit = Fit::Width;
    int m_sidebarMode = 1;
    int m_match = -1;
    bool m_restorePending = false;
};
