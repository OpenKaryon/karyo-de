// SPDX-License-Identifier: GPL-3.0-or-later
#include "lab.h"

#include <cmath>

namespace Lab {

namespace {

const QString Micro = QStringLiteral("µ");

} // namespace

const QList<Unit> &units(Family family)
{
    static const QList<Unit> mass = {
        {QStringLiteral("g"), 1, true},      {QStringLiteral("mg"), 1e-3, true}, {Micro + QStringLiteral("g"), 1e-6, true},
        {QStringLiteral("ng"), 1e-9, true},  {QStringLiteral("pg"), 1e-12, true},
    };
    static const QList<Unit> volume = {
        {QStringLiteral("l"), 1, true},
        {QStringLiteral("ml"), 1e-3, true},
        {Micro + QStringLiteral("l"), 1e-6, true},
        {QStringLiteral("nl"), 1e-9, false},  // pipettes go down to 0.1 µl
    };
    static const QList<Unit> molar = {
        {QStringLiteral("M"), 1, true},     {QStringLiteral("mM"), 1e-3, true}, {Micro + QStringLiteral("M"), 1e-6, true},
        {QStringLiteral("nM"), 1e-9, true}, {QStringLiteral("pM"), 1e-12, true},
    };
    static const QList<Unit> massConcentration = {
        {QStringLiteral("mg/ml"), 1, true},
        {QStringLiteral("g/l"), 1, false},
        {QStringLiteral("ng/") + Micro + QStringLiteral("l"), 1e-3, true},
        {Micro + QStringLiteral("g/ml"), 1e-3, false},
        {QStringLiteral("ng/ml"), 1e-6, true},
        {QStringLiteral("% (w/v)"), 10, false},
    };
    static const QList<Unit> weight = {
        {QStringLiteral("g/mol"), 1, true},
        {QStringLiteral("kDa"), 1000, false},
    };
    static const QList<Unit> length = {
        {QStringLiteral("bp"), 1, true},
        {QStringLiteral("kb"), 1000, false},
    };
    static const QList<Unit> fold = {
        {QStringLiteral("×"), 1, true},
    };
    switch (family) {
    case Family::Mass:
        return mass;
    case Family::Volume:
        return volume;
    case Family::Molar:
        return molar;
    case Family::MassConcentration:
        return massConcentration;
    case Family::Weight:
        return weight;
    case Family::Length:
        return length;
    case Family::Fold:
        break;
    }
    return fold;
}

Pick bestUnit(Family family, double base)
{
    const QList<Unit> &list = units(family);
    int smallest = -1;
    // Units go from large to small: the first one that shows at least 1.
    for (int i = 0; i < list.size(); ++i) {
        if (!list.at(i).preferred)
            continue;
        smallest = i;
        if (std::abs(base) / list.at(i).factor >= 1)
            return {i, base / list.at(i).factor};
    }
    if (base == 0 || smallest < 0)
        return {0, base / list.first().factor};
    return {smallest, base / list.at(smallest).factor};
}

double mass(double molar, double volume, double weight) { return molar * volume * weight; }

double molarConcentration(double mass, double volume, double weight) { return mass / (volume * weight); }

double volume(double mass, double molar, double weight) { return mass / (molar * weight); }

double molecularWeight(double mass, double molar, double volume) { return mass / (molar * volume); }

double dilute(Dilution unknown, double c1, double v1, double c2, double v2)
{
    switch (unknown) {
    case Dilution::C1:
        return c2 * v2 / v1;
    case Dilution::V1:
        return c2 * v2 / c1;
    case Dilution::C2:
        return c1 * v1 / v2;
    case Dilution::V2:
        break;
    }
    return c1 * v1 / c2;
}

double nucleicWeight(Strand strand, double length)
{
    switch (strand) {
    case Strand::DoubleDna:
        return length * 617.96 + 36.04;
    case Strand::SingleDna:
        return length * 308.97 + 18.02;
    case Strand::Rna:
        break;
    }
    return length * 320.5 + 159.0;
}

double nucleicMolar(Strand strand, double massConcentration, double length)
{
    return massConcentration / nucleicWeight(strand, length);
}

double nucleicMass(Strand strand, double molar, double length) { return molar * nucleicWeight(strand, length); }

double fromAbsorbance(Strand strand, double a260, double dilution)
{
    const double perUnit = strand == Strand::DoubleDna ? 50 : strand == Strand::SingleDna ? 33 : 40;  // µg/ml
    return a260 * perUnit * 1e-3 * dilution;
}

} // namespace Lab
