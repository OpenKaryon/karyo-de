// SPDX-License-Identifier: GPL-3.0-or-later
// Times as Clock shows them, in digits of one width so they do not jitter.
#pragma once

#include <QFont>
#include <QString>

namespace ClockFormat {

// 9:41, 1:00:00; seconds round up, so 0:00 is the end.
QString timer(qint64 ms);
// 00:41.27, 1:02:03.45
QString stopwatch(qint64 ms);
// The body font with tabular figures.
QFont digits(qreal px, int weight = QFont::Normal);

} // namespace ClockFormat
