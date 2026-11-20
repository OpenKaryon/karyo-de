// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDateTime>
#include <QLocale>
#include <QCoreApplication>
namespace ShellTime {
inline QString relativeTime(const QDateTime &when)
{
    if (!when.isValid())
        return {};
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 seconds = when.secsTo(now);
    const QLocale locale;
    if (seconds < 60)
        return QCoreApplication::translate("StartActions", "Just now");
    if (seconds < 3600)
        return QCoreApplication::translate("StartActions", "%n min ago", nullptr, int(seconds / 60));
    if (when.date() == now.date())
        return seconds < 6 * 3600 ? QCoreApplication::translate("StartActions", "%n h ago", nullptr, int(seconds / 3600))
                                 : QCoreApplication::translate("StartActions", "Today, %1").arg(locale.toString(when.time(), QLocale::ShortFormat));
    if (when.date() == now.date().addDays(-1))
        return QCoreApplication::translate("StartActions", "Yesterday, %1").arg(locale.toString(when.time(), QLocale::ShortFormat));
    return locale.toString(when.date(), QStringLiteral("MMM d"));
}
}
