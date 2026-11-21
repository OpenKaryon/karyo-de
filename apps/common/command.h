// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QStringList>
#include <functional>
class QObject;
namespace Karyo::Apps
{
// One literal POSIX shell argument (including empty strings and apostrophes).
QString shellQuote(QString value);
// Bounded asynchronous query. One success/failure, C.UTF-8 output, cancelled with
// its context. Do not use for privileged or persistent package transactions.
void runCommand(QObject *context, const QString &program, const QStringList &args,
    const std::function<void(const QString &)> &done,
    const std::function<void(const QString &)> &failed = { }, int timeoutMs = 15000);
}
