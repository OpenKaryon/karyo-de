// SPDX-License-Identifier: GPL-3.0-or-later
// StatusNotifierHost: tracks the registered tray items through a watcher,
// our own if no other one runs on the session bus.
#pragma once

#include <QList>
#include <QObject>

class StatusNotifierWatcher;
class TrayItem;

class TrayHost : public QObject
{
    Q_OBJECT
public:
    explicit TrayHost(QObject *parent = nullptr);

    // Items that are not Passive, in registration order.
    QList<TrayItem *> visibleItems() const;

Q_SIGNALS:
    void itemsChanged();

private Q_SLOTS:
    void addItem(const QString &id);
    void removeItem(const QString &id);

private:
    StatusNotifierWatcher *m_watcher = nullptr;
    QList<TrayItem *> m_items;
};
