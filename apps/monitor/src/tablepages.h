// SPDX-License-Identifier: GPL-3.0-or-later
// Table pages of System Monitor: Details, Users, Services and Startup apps.
#pragma once

#include <QWidget>

class QLabel;
class QStandardItemModel;
class QSortFilterProxyModel;
class QTreeView;
class Sampler;

// Heading, optional actions and a sortable table.
class TablePage : public QWidget
{
    Q_OBJECT
public:
    TablePage(const QString &title, const QStringList &columns, QWidget *parent = nullptr);
    void setFilter(const QString &text);

protected:
    void addAction(QWidget *button);
    void setRows(const QList<QStringList> &rows, const QList<QVariantList> &sortValues = {});
    QString selectedValue(int column) const;
    QTreeView *view() const { return m_view; }

private:
    class QHBoxLayout *m_actions;
    QStandardItemModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTreeView *m_view;
};

class DetailsPage : public TablePage
{
    Q_OBJECT
public:
    DetailsPage(Sampler *sampler, QWidget *parent = nullptr);

private:
    void refresh();
    Sampler *m_sampler;
};

class UsersPage : public TablePage
{
    Q_OBJECT
public:
    UsersPage(Sampler *sampler, QWidget *parent = nullptr);

private:
    void refresh();
    Sampler *m_sampler;
};

class ServicesPage : public TablePage
{
    Q_OBJECT
public:
    explicit ServicesPage(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void refresh();
    void control(const QString &verb);
    bool m_refreshing = false;
};

class StartupPage : public TablePage
{
    Q_OBJECT
public:
    explicit StartupPage(QWidget *parent = nullptr);

private:
    void refresh();
    void setEnabled(bool enabled);
};
