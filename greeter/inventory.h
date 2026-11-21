// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "desktopaccounts.h"
#include <QList>
#include <QStringList>
struct GreeterUser
{
    QString login, name, shell;
    quint32 uid = 0;
};
struct GreeterSession
{
    QString id, name;
    QStringList command;
    QString desktopNames;
    QStringList environment() const;
};
QList<GreeterUser> visibleGreeterUsers(const QList<GreeterUser> &users,
    const Karyo::Accounts::DesktopAccounts &accounts, QString *error = nullptr);
QList<GreeterUser> systemGreeterUsers(
    const Karyo::Accounts::DesktopAccounts &accounts, QString *error = nullptr);
QList<GreeterSession> greeterSessions(
    const QString &directory = QStringLiteral("/usr/share/wayland-sessions"));
struct GreeterSelection
{
    QString user, session, error;
};
// The new file is authoritative; use legacy state only if it is absent.
GreeterSelection readGreeterSelection(const QString &configRoot = { }, const QString &homeRoot = { });
bool saveGreeterSelection(
    const QString &user, const QString &session, QString *error = nullptr, const QString &configRoot = { });
