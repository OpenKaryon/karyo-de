// SPDX-License-Identifier: GPL-3.0-or-later
// One org.kde.StatusNotifierItem: icon, tooltip, and its activation actions.
#pragma once

#include <QIcon>
#include <QObject>
#include <QPoint>
#include <QTimer>

class TrayItem : public QObject
{
    Q_OBJECT
public:
    // id is "<service>/<object path>" as registered with the watcher.
    explicit TrayItem(const QString &id, QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString title() const { return m_title; }
    QString status() const { return m_status; }
    QIcon icon() const;
    QString toolTipTitle() const;
    QString toolTipText() const { return m_toolTipText; }
    bool isPassive() const { return m_status == QLatin1String("Passive"); }
    bool isNetworkApplet() const { return m_title == QLatin1String("nm-tray"); }
    bool activationOpensMenu() const { return m_itemIsMenu && !m_menuPath.isEmpty(); }

    // parentWidget anchors menus, which are popups of its window.
    void activate(const QPoint &globalPos, QWidget *parentWidget);
    void secondaryActivate(const QPoint &globalPos);
    void showContextMenu(const QPoint &globalPos, QWidget *parentWidget);

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void scheduleRefresh();

private:
    void refresh();
    QIcon iconFromName(const QString &name) const;

    QString m_id;
    QString m_service;
    QString m_path;
    QString m_applicationId;
    QString m_title;
    QString m_status;
    QString m_iconName;
    QString m_attentionIconName;
    QString m_iconThemePath;
    QIcon m_iconPixmaps;
    QIcon m_attentionPixmaps;
    QString m_toolTipTitle;
    QString m_toolTipText;
    QString m_menuPath;
    bool m_itemIsMenu = false;
    QTimer m_refreshTimer;
};
