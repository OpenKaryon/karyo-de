// SPDX-License-Identifier: GPL-3.0-or-later
// Client side of com.canonical.dbusmenu, enough to show a tray item's menu as a QMenu.
#pragma once

#include <QPoint>
#include <QString>

class QMenu;
class QWidget;

namespace DBusMenu {

// Dispatch a visible, enabled top-level action by its label.
bool activateAction(const QString &service, const QString &path, const QString &name);
// Fetches the layout, shows it at globalPos and reports the clicked entry back.
void popup(const QString &service, const QString &path, const QPoint &globalPos, QWidget *parent);

} // namespace DBusMenu
