// SPDX-License-Identifier: GPL-3.0-or-later
// Processes page (design 1k).
#pragma once

#include "sampler.h"
#include <QSet>
#include <QWidget>

class ProcessModel;
class QTreeView;
class Sampler;
class TotalsHeader;

class ProcessesPage : public QWidget
{
    Q_OBJECT
public:
    ProcessesPage(Sampler *sampler, QWidget *parent = nullptr);
    void setFilter(const QString &text);

private:
    QList<ProcessInfo> selectedProcesses() const;
    void applyAction(const QList<ProcessInfo> &processes, int signal);
    void endTask();
    void runNewTask();
    void efficiencyMode();
    void showViewMenu();
    void showContextMenu(const QPoint &pos);
    void saveState();
    void restoreState();

    Sampler *m_sampler;
    ProcessModel *m_model;
    QTreeView *m_view;
    TotalsHeader *m_header;
    QSet<QString> m_expanded;
    QString m_selectedKey;
    int m_scroll = 0;
};
