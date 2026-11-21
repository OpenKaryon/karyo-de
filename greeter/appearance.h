// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
struct GreeterAppearance
{
    QString theme, accent;
    bool accepted = false;
};
// Published appearance contains only public theme choices, never a home profile.
GreeterAppearance readGreeterAppearance(const QString &login, quint32 uid,
    const QString &directory = QStringLiteral("/var/lib/karyo/appearance"),
    const QString &legacyDirectory = QStringLiteral("/var/lib/openkaryon/appearance"));
bool applyGreeterAppearance(const GreeterAppearance &appearance, const QString &settingsPath = { });
