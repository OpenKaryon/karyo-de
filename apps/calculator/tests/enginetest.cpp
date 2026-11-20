// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the calculator's expression engine and the lab formulas.
#include "engine.h"
#include "lab.h"

#include <QCoreApplication>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void expect(const QString &expression, double expected, Calc::Angle angle = Calc::Angle::Degrees)
{
    const Calc::Result result = Calc::evaluate(expression, angle, 42);
    const bool ok = result.ok && std::abs(result.value - expected) <= 1e-9 * std::max(1.0, std::abs(expected));
    if (!ok) {
        std::printf("FAIL %s = %s (expected %.12g)\n", qPrintable(expression),
                    qPrintable(result.ok ? QString::number(result.value, 'g', 15) : result.error), expected);
        ++failures;
    }
}

void expectError(const QString &expression)
{
    const Calc::Result result = Calc::evaluate(expression);
    if (result.ok) {
        std::printf("FAIL %s should be an error, got %.12g\n", qPrintable(expression), result.value);
        ++failures;
    }
}

void expectText(const QString &actual, const QString &expected)
{
    if (actual != expected) {
        std::printf("FAIL \"%s\" (expected \"%s\")\n", qPrintable(actual), qPrintable(expected));
        ++failures;
    }
}

void expectNear(const char *what, double actual, double expected, double tolerance = 1e-9)
{
    if (std::abs(actual - expected) > tolerance * std::max(1.0, std::abs(expected))) {
        std::printf("FAIL %s: %.12g (expected %.12g)\n", what, actual, expected);
        ++failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    expect(QStringLiteral("1+2*3"), 7);
    expect(QStringLiteral("(1+2)*3"), 9);
    expect(QStringLiteral("2^3^2"), 512);
    expect(QStringLiteral("-2^2"), -4);
    expect(QStringLiteral("2^-1"), 0.5);
    expect(QStringLiteral("7÷2"), 3.5);
    expect(QStringLiteral("3×4−5"), 7);
    expect(QStringLiteral("0.1+0.2"), 0.3);
    expect(QStringLiteral("1,5*2"), 3);
    expect(QStringLiteral("1.5e-3*1000"), 1.5);
    expect(QStringLiteral("2e"), 2 * std::exp(1.0));
    expect(QStringLiteral("2π"), 2 * M_PI);
    expect(QStringLiteral("3(1+2)"), 9);
    expect(QStringLiteral("(1+2)(3+4)"), 21);
    expect(QStringLiteral("50+10%"), 55);
    expect(QStringLiteral("50-10%"), 45);
    expect(QStringLiteral("200*10%"), 20);
    expect(QStringLiteral("10%"), 0.1);
    expect(QStringLiteral("5!"), 120);
    expect(QStringLiteral("0!"), 1);
    expect(QStringLiteral("3²"), 9);
    expect(QStringLiteral("√16"), 4);
    expect(QStringLiteral("√(9+16)"), 5);
    expect(QStringLiteral("sqrt(2)^2"), 2);
    expect(QStringLiteral("sin30"), 0.5);
    expect(QStringLiteral("sin(180)"), 0);
    expect(QStringLiteral("cos(60)"), 0.5);
    expect(QStringLiteral("tan(45)"), 1);
    expect(QStringLiteral("asin(1)"), 90);
    expect(QStringLiteral("sin(pi/2)"), 1, Calc::Angle::Radians);
    expect(QStringLiteral("ln(e)"), 1);
    expect(QStringLiteral("log(1000)"), 3);
    expect(QStringLiteral("2sin30"), 1);
    expect(QStringLiteral("ans+1"), 43);
    expect(QStringLiteral("abs(-3)"), 3);
    expect(QStringLiteral("(2+3"), 5);
    expect(QStringLiteral("−(−2)"), 2);
    expectError(QString());
    expectError(QStringLiteral("1/0"));
    expectError(QStringLiteral("1+"));
    expectError(QStringLiteral("2**3"));
    expectError(QStringLiteral("foo(2)"));
    expectError(QStringLiteral("√-1"));
    expectError(QStringLiteral("ln(0)"));
    expectError(QStringLiteral("tan(90)"));
    expectError(QStringLiteral("171!"));
    expectError(QStringLiteral("1.2.3"));
    expectError(QStringLiteral("(1))"));

    expectText(Calc::format(0.1 + 0.2), QStringLiteral("0.3"));
    expectText(Calc::format(-4), QStringLiteral("−4"));
    expectText(Calc::format(1e15), QStringLiteral("1e15"));
    expectText(Calc::format(1.5e-7), QStringLiteral("1.5e−7"));
    expectText(Calc::format(123456.789), QStringLiteral("123456.789"));
    expectText(Calc::format(2.0 / 3), QStringLiteral("0.666666666667"));
    expect(Calc::format(-1.5e-7), -1.5e-7);  // the display reads back

    // NaCl 58.44 g/mol: 1 M in 100 ml takes 5.844 g.
    expectNear("molarity mass", Lab::mass(1, 0.1, 58.44), 5.844);
    expectNear("molarity concentration", Lab::molarConcentration(5.844, 0.1, 58.44), 1);
    expectNear("molarity volume", Lab::volume(5.844, 1, 58.44), 0.1);
    expectNear("molecular weight", Lab::molecularWeight(5.844, 1, 0.1), 58.44);
    // C1V1 = C2V2: 10 mM to 100 µM in 1 ml takes 10 µl.
    expectNear("dilution V1", Lab::dilute(Lab::Dilution::V1, 10e-3, 0, 100e-6, 1e-3), 10e-6);
    expectNear("dilution C2", Lab::dilute(Lab::Dilution::C2, 10e-3, 10e-6, 0, 1e-3), 100e-6);
    expectNear("dilution V2", Lab::dilute(Lab::Dilution::V2, 10e-3, 10e-6, 100e-6, 0), 1e-3);
    expectNear("dilution C1", Lab::dilute(Lab::Dilution::C1, 0, 10e-6, 100e-6, 1e-3), 10e-3);
    // NEB: a 500 bp dsDNA weighs 500·617.96 + 36.04 g/mol; 50 ng/µl of it is 161.8 nM.
    expectNear("dsDNA weight", Lab::nucleicWeight(Lab::Strand::DoubleDna, 500), 309016.04);
    expectNear("dsDNA nM", Lab::nucleicMolar(Lab::Strand::DoubleDna, 50e-3, 500) * 1e9, 161.8040, 1e-5);
    expectNear("dsDNA ng/ul", Lab::nucleicMass(Lab::Strand::DoubleDna, 161.8040e-9, 500) * 1e3, 50, 1e-5);
    expectNear("RNA weight", Lab::nucleicWeight(Lab::Strand::Rna, 100), 32209);
    // A260 of 0.5 at a 1:10 dilution: dsDNA 250 ng/µl.
    expectNear("A260", Lab::fromAbsorbance(Lab::Strand::DoubleDna, 0.5, 10) * 1e3, 250);

    const Lab::Pick pick = Lab::bestUnit(Lab::Family::Mass, 0.005844);
    expectText(Lab::units(Lab::Family::Mass).at(pick.unit).name, QStringLiteral("mg"));
    expectNear("best unit value", pick.value, 5.844);

    if (failures == 0)
        std::printf("all passed\n");
    return failures == 0 ? 0 : 1;
}
