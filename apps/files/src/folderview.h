// SPDX-License-Identifier: GPL-3.0-or-later
// One tab of Files: a folder in details or icons view, with its history.
#pragma once

#include <QFileInfo>
#include <QStringList>
#include <QSet>
#include <QWidget>

class QAbstractItemView;
class QFileSystemModel;
class QListView;
class QModelIndex;
class QStackedWidget;
class QTreeView;
class FolderProxy;

class FolderView : public QWidget
{
    Q_OBJECT
public:
    enum class Mode { Details, Icons };

    explicit FolderView(QWidget *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path, bool record = true);
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    void back();
    void forward();
    void up();
    void refresh();

    void setFilter(const QString &text);
    void setShowHidden(bool show);
    bool showHidden() const;
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    void sortBy(int column, Qt::SortOrder order);
    int sortColumn() const;
    Qt::SortOrder sortOrder() const;

    QStringList selectedPaths() const;
    QList<QFileInfo> selectedInfos() const;
    int itemCount() const;
    void selectPath(const QString &path, bool edit = false);
    void selectPaths(const QStringList &paths, bool edit = false);
    void renameSelected();
    void selectAll();
    QFileSystemModel *model() const { return m_model; }

Q_SIGNALS:
    void pathChanged(const QString &path);
    void selectionChanged();
    void itemClicked();
    void refreshRequested();
    void fileActivated(const QString &path);
    void contextMenuRequested(const QPoint &globalPos, bool onItem);
    void loaded();
    // Files dropped on the folder or on a folder in it, to copy or move there.
    void filesDropped(const QStringList &sources, const QString &targetDir, bool move);

private:
    QAbstractItemView *currentView() const;
    QModelIndex sourceIndex(const QModelIndex &proxyIndex) const;
    void activate(const QModelIndex &proxyIndex);

    QFileSystemModel *m_model;
    FolderProxy *m_proxy;
    QStackedWidget *m_stack;
    QTreeView *m_details;
    QListView *m_icons;
    QString m_path;
    QStringList m_back;
    QStringList m_forward;
    Mode m_mode = Mode::Details;
    QSet<QString> m_loadedFolders;
    QStringList m_pendingSelect;
    bool m_pendingEdit = false;
};
