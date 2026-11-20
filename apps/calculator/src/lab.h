// SPDX-License-Identifier: GPL-3.0-or-later
// Lab arithmetic: units and the formulas behind the Molarity, Dilution and
// Nucleic acids pages. Values are in base units: g, l, mol/l (M), g/l,
// g/mol, bases (bp or nt) and fold.
#pragma once

#include <QList>
#include <QString>

namespace Lab {

enum class Family { Mass, Volume, Molar, MassConcentration, Weight, Length, Fold };

struct Unit
{
    QString name;
    double factor;     // base units per unit
    bool preferred;    // offered when a result picks its unit
};

const QList<Unit> &units(Family family);

struct Pick
{
    int unit;
    double value;
};
// The unit that shows a value (in base units) as 1 to 1000 where it can.
Pick bestUnit(Family family, double base);

// mass = concentration × volume × molecular weight
double mass(double molar, double volume, double weight);
double molarConcentration(double mass, double volume, double weight);
double volume(double mass, double molar, double weight);
double molecularWeight(double mass, double molar, double volume);

// C1·V1 = C2·V2, solved for one of them (its argument is ignored).
enum class Dilution { C1, V1, C2, V2 };
double dilute(Dilution unknown, double c1, double v1, double c2, double v2);

// Molecular weights after NEB: dsDNA 617.96 g/mol per bp + 36.04,
// ssDNA 308.97 per nt + 18.02, ssRNA 320.5 per nt + 159.0.
enum class Strand { DoubleDna, SingleDna, Rna };
double nucleicWeight(Strand strand, double length);
double nucleicMolar(Strand strand, double massConcentration, double length);
double nucleicMass(Strand strand, double molar, double length);
// g/l from A260 × dilution: 50 µg/ml (dsDNA), 33 (ssDNA) or 40 (RNA) per unit.
double fromAbsorbance(Strand strand, double a260, double dilution);

} // namespace Lab
