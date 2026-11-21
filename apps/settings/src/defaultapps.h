// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
namespace DefaultApps {
// Atomically update only selected default MIME entries in the user's file.
bool set(const QStringList &mimeTypes, const QString &desktopId, QString *error = nullptr);
}
