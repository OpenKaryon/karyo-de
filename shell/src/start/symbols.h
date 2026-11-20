// SPDX-License-Identifier: GPL-3.0-or-later
// Symbols Start finds by name, as Windows' emoji and symbol panel does:
// "micro" gives µ, "degree" ° and °C, "alpha" α, "times" ×, "5 prime" 5′.
// Greek letters, units, math, super- and subscripts, arrows, typography and
// currency, searched in English and in Russian.
#pragma once

#include <QList>
#include <QString>

struct SymbolHit
{
    QString text;  // what is copied: one character or a few (µl, °C)
    QString name;  // translated
    bool exact = false;  // a whole word of the name or a keyword matched
};

// Nothing for queries under two characters.
QList<SymbolHit> searchSymbols(const QString &query, int limit);
// "U+00B5" or "U+00B0 U+0043".
QString symbolCodes(const QString &text);
