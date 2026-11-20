// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <karyo/ui/desktopentry.h>
#include <QProcess>
#include <QFileInfo>
#include <QStandardPaths>
namespace Integrations {
inline QString settingsProgram(bool network)
{
    const QStringList programs = network
        ? QStringList{QStringLiteral("karyo-settings"), QStringLiteral("openkaryon-settings"),
                      QStringLiteral("nm-connection-editor")}
        : QStringList{QStringLiteral("karyo-settings"), QStringLiteral("openkaryon-settings"),
                      QStringLiteral("systemsettings"), QStringLiteral("gnome-control-center"),
                      QStringLiteral("xfce4-settings-manager"), QStringLiteral("lxqt-config")};
    for (const QString &program : programs) {
        const QString found = QStandardPaths::findExecutable(program);
        if (!found.isEmpty())
            return found;
    }
    return {};
}
inline bool openSettings(bool network = false)
{
    const QString program = settingsProgram(network);
    if (program.isEmpty())
        return false;
    const QString name = QFileInfo(program).fileName();
    const bool own = name == QLatin1String("karyo-settings") || name == QLatin1String("openkaryon-settings");
    return QProcess::startDetached(program, network && own ? QStringList{QStringLiteral("network")} : QStringList{});
}
}
