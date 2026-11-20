// SPDX-License-Identifier: GPL-3.0-or-later
#include "clockformat.h"

#include <karyo/ui/theme.h>

namespace ClockFormat {

QString timer(qint64 ms)
{
    const qint64 seconds = (qMax<qint64>(0, ms) + 999) / 1000;
    const qint64 h = seconds / 3600, m = seconds / 60 % 60, s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString stopwatch(qint64 ms)
{
    const qint64 centi = qMax<qint64>(0, ms) / 10;
    const qint64 h = centi / 360000, m = centi / 6000 % 60, s = centi / 100 % 60, c = centi % 100;
    const QString rest = QStringLiteral("%1.%2").arg(s, 2, 10, QLatin1Char('0')).arg(c, 2, 10, QLatin1Char('0'));
    if (h > 0)
        return QStringLiteral("%1:%2:").arg(h).arg(m, 2, 10, QLatin1Char('0')) + rest;
    return QStringLiteral("%1:").arg(m, 2, 10, QLatin1Char('0')) + rest;
}

QFont digits(qreal px, int weight)
{
    QFont font = Karyo::Ui::bodyFont(px, weight);
    font.setFeature(QFont::Tag("tnum"), 1);
    return font;
}

} // namespace ClockFormat
