// SPDX-License-Identifier: GPL-3.0-or-later
// The calculator's expression language: + − × ÷ ^ and parentheses, n!,
// percent (50 + 10% is 55, 200 × 10% is 20), functions (sin cos tan asin
// acos atan ln log sqrt √ abs exp), π and e, ans (the last result), 1.5e-3
// and implicit multiplication (2π, 3(1+2), 2sin30).
#pragma once

#include <QString>

namespace Calc {

enum class Angle { Degrees, Radians };

struct Result
{
    bool ok = false;
    double value = 0;
    QString error;  // translated, for the display
};

Result evaluate(const QString &expression, Angle angle = Angle::Degrees, double ans = 0);

// Up to `significant` digits, scientific notation for very large or small
// values, a typographic minus; evaluate() reads it back.
QString format(double value, int significant = 12);

// The expression with * / - shown as × ÷ −.
QString pretty(const QString &expression);

} // namespace Calc
