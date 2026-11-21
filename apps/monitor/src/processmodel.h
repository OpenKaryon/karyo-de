// SPDX-License-Identifier: GPL-3.0-or-later
// Processes grouped like Task Manager: Apps (processes started from a desktop
// entry, with their child processes) and Background processes (one row per
// service, with its children).
#pragma once

#include "sampler.h"

#include <karyo/ui/appstyle.h>

#include <QAbstractItemModel>
#include <QList>

#include <memory>

class ProcessModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column { Name, Environment, Cpu, Memory, Disk, Network, ColumnCount };
    enum Role { KeyRole = Qt::UserRole + 1, GroupRole, ValueRole, StyleRole, PidsRole };

    explicit ProcessModel(Sampler *sampler, QObject *parent = nullptr);

    void setFilter(const QString &text);
    void sort(int column, Qt::SortOrder order) override;

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    QModelIndex indexForKey(const QString &key) const;
    int appCount() const;
    int backgroundCount() const;

Q_SIGNALS:
    void aboutToRebuild();
    void rebuilt();

private:
    struct Node
    {
        QString key;
        QString name;
        QString environment;
        AppStyle style;
        bool hasStyle = false;
        bool group = false;
        double cpu = 0;
        qint64 memory = 0;
        double disk = 0;
        QList<pid_t> pids;
        Node *parent = nullptr;
        QList<std::shared_ptr<Node>> children;
    };

    void rebuild();
    void sortNodes(QList<std::shared_ptr<Node>> &nodes) const;
    Node *nodeFor(const QModelIndex &index) const;
    QModelIndex indexFor(Node *node, int column) const;

    Sampler *m_sampler;
    QString m_filter;
    int m_sortColumn = Name;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QList<std::shared_ptr<Node>> m_groups;
};

QString formatBytes(double bytes);
QString formatRate(double bytesPerSec);
