// SPDX-License-Identifier: GPL-3.0-or-later
// Where Settings keeps its compatible per-user choices. The shell and the apps read the same
// files: desktop.conf (look), shell.conf (taskbar, Start) and session.env
// (language and keyboard, read by karyo-session at sign-in).
#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace Config {

QSettings desktop();
QSettings shell();

// Prefer ~/.config/karyo/session.env, reading the legacy file until the first edit.
QString sessionValue(const QString &key);
bool setSessionValue(const QString &key, const QString &value, QString *error = nullptr);

// The user's display name: the GECOS full name, or the login.
QString userName();
QString userLogin();
QString initials(const QString &name);
// Member of sudo (Debian's administrators).
bool isAdministrator();

} // namespace Config
