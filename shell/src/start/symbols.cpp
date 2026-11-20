// SPDX-License-Identifier: GPL-3.0-or-later
#include "symbols.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

namespace {

class Text
{
    Q_DECLARE_TR_FUNCTIONS(Symbol)
};

struct Symbol
{
    const char *text;
    const char *name;      // English, translated for display
    const char *keywords;  // more words to find it by, English and Russian
};

// clang-format off
const Symbol Table[] = {
    // Units and the bench.
    {"µ", QT_TRANSLATE_NOOP("Symbol", "Micro sign"), "micro mu u микро мю мк"},
    {"µl", QT_TRANSLATE_NOOP("Symbol", "Microliter"), "microlitre ul uL микролитр мкл"},
    {"µg", QT_TRANSLATE_NOOP("Symbol", "Microgram"), "ug микрограмм мкг"},
    {"µm", QT_TRANSLATE_NOOP("Symbol", "Micrometer"), "micron um микрометр микрон мкм"},
    {"µM", QT_TRANSLATE_NOOP("Symbol", "Micromolar"), "uM микромоль микромолярный мкм"},
    {"°", QT_TRANSLATE_NOOP("Symbol", "Degree sign"), "degree deg градус"},
    {"°C", QT_TRANSLATE_NOOP("Symbol", "Degree Celsius"), "celsius centigrade temperature градус цельсий температура"},
    {"°F", QT_TRANSLATE_NOOP("Symbol", "Degree Fahrenheit"), "fahrenheit градус фаренгейт"},
    {"Å", QT_TRANSLATE_NOOP("Symbol", "Angstrom"), "angstrom ангстрем"},
    {"×g", QT_TRANSLATE_NOOP("Symbol", "Times g (centrifugal force)"), "rcf xg centrifuge force центрифуга перегрузка"},
    {"5′", QT_TRANSLATE_NOOP("Symbol", "Five prime end"), "5 prime five end dna rna конец 5"},
    {"3′", QT_TRANSLATE_NOOP("Symbol", "Three prime end"), "3 prime three end dna rna конец 3"},
    {"‰", QT_TRANSLATE_NOOP("Symbol", "Per mille"), "permille промилле"},
    {"Ω", QT_TRANSLATE_NOOP("Symbol", "Ohm sign"), "ohm omega resistance ом сопротивление"},
    {"♀", QT_TRANSLATE_NOOP("Symbol", "Female"), "female venus woman самка женский"},
    {"♂", QT_TRANSLATE_NOOP("Symbol", "Male"), "male mars man самец мужской"},
    // Math.
    {"×", QT_TRANSLATE_NOOP("Symbol", "Multiplication sign"), "times multiply cross x умножить умножение крест"},
    {"÷", QT_TRANSLATE_NOOP("Symbol", "Division sign"), "divide делить деление"},
    {"±", QT_TRANSLATE_NOOP("Symbol", "Plus-minus sign"), "plus minus pm плюс минус"},
    {"−", QT_TRANSLATE_NOOP("Symbol", "Minus sign"), "minus минус"},
    {"·", QT_TRANSLATE_NOOP("Symbol", "Middle dot"), "dot center точка середина"},
    {"≈", QT_TRANSLATE_NOOP("Symbol", "Almost equal to"), "approximately about approx примерно приблизительно"},
    {"≠", QT_TRANSLATE_NOOP("Symbol", "Not equal to"), "unequal не равно неравно"},
    {"≤", QT_TRANSLATE_NOOP("Symbol", "Less-than or equal to"), "less equal меньше равно"},
    {"≥", QT_TRANSLATE_NOOP("Symbol", "Greater-than or equal to"), "greater equal больше равно"},
    {"≪", QT_TRANSLATE_NOOP("Symbol", "Much less-than"), "much less много меньше"},
    {"≫", QT_TRANSLATE_NOOP("Symbol", "Much greater-than"), "much greater много больше"},
    {"∞", QT_TRANSLATE_NOOP("Symbol", "Infinity"), "infinite бесконечность"},
    {"√", QT_TRANSLATE_NOOP("Symbol", "Square root"), "root sqrt корень"},
    {"∑", QT_TRANSLATE_NOOP("Symbol", "Summation"), "sum sigma сумма"},
    {"∏", QT_TRANSLATE_NOOP("Symbol", "Product"), "product pi произведение"},
    {"∫", QT_TRANSLATE_NOOP("Symbol", "Integral"), "integral интеграл"},
    {"∂", QT_TRANSLATE_NOOP("Symbol", "Partial derivative"), "partial derivative частная производная"},
    {"∆", QT_TRANSLATE_NOOP("Symbol", "Increment"), "delta change difference приращение дельта изменение"},
    {"∝", QT_TRANSLATE_NOOP("Symbol", "Proportional to"), "proportional пропорционально"},
    {"∈", QT_TRANSLATE_NOOP("Symbol", "Element of"), "in element member принадлежит"},
    {"∩", QT_TRANSLATE_NOOP("Symbol", "Intersection"), "intersection and пересечение"},
    {"∪", QT_TRANSLATE_NOOP("Symbol", "Union"), "union or объединение"},
    {"′", QT_TRANSLATE_NOOP("Symbol", "Prime"), "prime minute штрих минута"},
    {"″", QT_TRANSLATE_NOOP("Symbol", "Double prime"), "double prime second два штриха секунда"},
    {"%", QT_TRANSLATE_NOOP("Symbol", "Percent sign"), "percent процент"},
    // Superscripts and subscripts.
    {"²", QT_TRANSLATE_NOOP("Symbol", "Superscript two"), "squared power 2 квадрат степень надстрочный"},
    {"³", QT_TRANSLATE_NOOP("Symbol", "Superscript three"), "cubed power 3 куб степень надстрочный"},
    {"¹", QT_TRANSLATE_NOOP("Symbol", "Superscript one"), "power 1 степень надстрочный"},
    {"⁰", QT_TRANSLATE_NOOP("Symbol", "Superscript zero"), "power 0 степень надстрочный"},
    {"⁴", QT_TRANSLATE_NOOP("Symbol", "Superscript four"), "power 4 степень надстрочный"},
    {"⁵", QT_TRANSLATE_NOOP("Symbol", "Superscript five"), "power 5 степень надстрочный"},
    {"⁶", QT_TRANSLATE_NOOP("Symbol", "Superscript six"), "power 6 степень надстрочный"},
    {"⁷", QT_TRANSLATE_NOOP("Symbol", "Superscript seven"), "power 7 степень надстрочный"},
    {"⁸", QT_TRANSLATE_NOOP("Symbol", "Superscript eight"), "power 8 степень надстрочный"},
    {"⁹", QT_TRANSLATE_NOOP("Symbol", "Superscript nine"), "power 9 степень надстрочный"},
    {"⁺", QT_TRANSLATE_NOOP("Symbol", "Superscript plus"), "cation ion charge positive катион ион заряд надстрочный"},
    {"⁻", QT_TRANSLATE_NOOP("Symbol", "Superscript minus"), "anion ion charge negative exponent анион ион заряд надстрочный"},
    {"₀", QT_TRANSLATE_NOOP("Symbol", "Subscript zero"), "index 0 индекс подстрочный"},
    {"₁", QT_TRANSLATE_NOOP("Symbol", "Subscript one"), "index 1 индекс подстрочный"},
    {"₂", QT_TRANSLATE_NOOP("Symbol", "Subscript two"), "index 2 co2 h2o индекс подстрочный"},
    {"₃", QT_TRANSLATE_NOOP("Symbol", "Subscript three"), "index 3 индекс подстрочный"},
    {"₄", QT_TRANSLATE_NOOP("Symbol", "Subscript four"), "index 4 индекс подстрочный"},
    {"₅", QT_TRANSLATE_NOOP("Symbol", "Subscript five"), "index 5 индекс подстрочный"},
    {"₆", QT_TRANSLATE_NOOP("Symbol", "Subscript six"), "index 6 индекс подстрочный"},
    {"₇", QT_TRANSLATE_NOOP("Symbol", "Subscript seven"), "index 7 индекс подстрочный"},
    {"₈", QT_TRANSLATE_NOOP("Symbol", "Subscript eight"), "index 8 индекс подстрочный"},
    {"₉", QT_TRANSLATE_NOOP("Symbol", "Subscript nine"), "index 9 индекс подстрочный"},
    // Greek.
    {"α", QT_TRANSLATE_NOOP("Symbol", "Greek small letter alpha"), "alpha альфа"},
    {"β", QT_TRANSLATE_NOOP("Symbol", "Greek small letter beta"), "beta бета"},
    {"γ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter gamma"), "gamma гамма"},
    {"δ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter delta"), "delta дельта"},
    {"ε", QT_TRANSLATE_NOOP("Symbol", "Greek small letter epsilon"), "epsilon эпсилон"},
    {"ζ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter zeta"), "zeta дзета"},
    {"η", QT_TRANSLATE_NOOP("Symbol", "Greek small letter eta"), "eta эта"},
    {"θ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter theta"), "theta тета"},
    {"κ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter kappa"), "kappa каппа"},
    {"λ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter lambda"), "lambda wavelength лямбда длина волны"},
    {"μ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter mu"), "mu мю"},
    {"ν", QT_TRANSLATE_NOOP("Symbol", "Greek small letter nu"), "nu ню"},
    {"ξ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter xi"), "xi кси"},
    {"π", QT_TRANSLATE_NOOP("Symbol", "Greek small letter pi"), "pi пи"},
    {"ρ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter rho"), "rho density ро плотность"},
    {"σ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter sigma"), "sigma standard deviation сигма отклонение"},
    {"τ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter tau"), "tau тау"},
    {"φ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter phi"), "phi фи"},
    {"χ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter chi"), "chi хи"},
    {"ψ", QT_TRANSLATE_NOOP("Symbol", "Greek small letter psi"), "psi пси"},
    {"ω", QT_TRANSLATE_NOOP("Symbol", "Greek small letter omega"), "omega омега"},
    {"Γ", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter gamma"), "gamma гамма"},
    {"Δ", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter delta"), "delta deletion дельта делеция"},
    {"Θ", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter theta"), "theta тета"},
    {"Λ", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter lambda"), "lambda лямбда"},
    {"Π", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter pi"), "pi пи"},
    {"Σ", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter sigma"), "sigma сигма"},
    {"Φ", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter phi"), "phi фи"},
    {"Ψ", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter psi"), "psi пси"},
    {"Ω", QT_TRANSLATE_NOOP("Symbol", "Greek capital letter omega"), "omega омега"},
    // Arrows.
    {"→", QT_TRANSLATE_NOOP("Symbol", "Rightwards arrow"), "arrow right to reaction стрелка вправо реакция"},
    {"←", QT_TRANSLATE_NOOP("Symbol", "Leftwards arrow"), "arrow left стрелка влево"},
    {"↑", QT_TRANSLATE_NOOP("Symbol", "Upwards arrow"), "arrow up increase upregulated стрелка вверх рост"},
    {"↓", QT_TRANSLATE_NOOP("Symbol", "Downwards arrow"), "arrow down decrease downregulated стрелка вниз снижение"},
    {"↔", QT_TRANSLATE_NOOP("Symbol", "Left right arrow"), "arrow both стрелка двойная"},
    {"⇌", QT_TRANSLATE_NOOP("Symbol", "Equilibrium"), "equilibrium reversible arrow равновесие обратимая стрелка"},
    {"⇒", QT_TRANSLATE_NOOP("Symbol", "Rightwards double arrow"), "implies then arrow следует стрелка"},
    // Typography.
    {"—", QT_TRANSLATE_NOOP("Symbol", "Em dash"), "dash long тире длинное"},
    {"–", QT_TRANSLATE_NOOP("Symbol", "En dash"), "dash range короткое тире диапазон"},
    {"…", QT_TRANSLATE_NOOP("Symbol", "Ellipsis"), "dots многоточие"},
    {"«»", QT_TRANSLATE_NOOP("Symbol", "Guillemets"), "quotes angle кавычки ёлочки елочки"},
    {"„“", QT_TRANSLATE_NOOP("Symbol", "German quotation marks"), "quotes low кавычки лапки"},
    {"“”", QT_TRANSLATE_NOOP("Symbol", "Quotation marks"), "quotes english кавычки английские"},
    {"’", QT_TRANSLATE_NOOP("Symbol", "Apostrophe"), "apostrophe quote апостроф"},
    {"•", QT_TRANSLATE_NOOP("Symbol", "Bullet"), "bullet dot point маркер точка"},
    {"§", QT_TRANSLATE_NOOP("Symbol", "Section sign"), "section paragraph параграф"},
    {"¶", QT_TRANSLATE_NOOP("Symbol", "Pilcrow"), "paragraph pilcrow абзац"},
    {"©", QT_TRANSLATE_NOOP("Symbol", "Copyright sign"), "copyright копирайт"},
    {"®", QT_TRANSLATE_NOOP("Symbol", "Registered sign"), "registered зарегистрировано"},
    {"™", QT_TRANSLATE_NOOP("Symbol", "Trade mark sign"), "trademark tm товарный знак"},
    {"№", QT_TRANSLATE_NOOP("Symbol", "Numero sign"), "number numero номер"},
    {"†", QT_TRANSLATE_NOOP("Symbol", "Dagger"), "dagger cross footnote крестик сноска"},
    {"✓", QT_TRANSLATE_NOOP("Symbol", "Check mark"), "check tick yes done галочка готово"},
    {"✗", QT_TRANSLATE_NOOP("Symbol", "Ballot X"), "cross no fail крестик нет"},
    {"★", QT_TRANSLATE_NOOP("Symbol", "Black star"), "star звезда"},
    // Currency.
    {"€", QT_TRANSLATE_NOOP("Symbol", "Euro sign"), "euro евро"},
    {"£", QT_TRANSLATE_NOOP("Symbol", "Pound sign"), "pound фунт"},
    {"¥", QT_TRANSLATE_NOOP("Symbol", "Yen sign"), "yen yuan иена юань"},
    {"₽", QT_TRANSLATE_NOOP("Symbol", "Ruble sign"), "ruble rouble рубль"},
    {"₸", QT_TRANSLATE_NOOP("Symbol", "Tenge sign"), "tenge тенге"},
    {"$", QT_TRANSLATE_NOOP("Symbol", "Dollar sign"), "dollar доллар"},
    {"¢", QT_TRANSLATE_NOOP("Symbol", "Cent sign"), "cent цент"},
};
// clang-format on

QStringList wordsOf(const QString &text)
{
    return text.toLower().split(QRegularExpression(QStringLiteral("[\\s()-]+")), Qt::SkipEmptyParts);
}

} // namespace

QList<SymbolHit> searchSymbols(const QString &query, int limit)
{
    QList<SymbolHit> exact;
    QList<SymbolHit> partial;
    const QString trimmed = query.trimmed();
    if (trimmed.size() < 2)
        return {};
    const QStringList tokens = wordsOf(trimmed);
    for (const Symbol &symbol : Table) {
        const QString text = QString::fromUtf8(symbol.text);
        const QString name = Text::tr(symbol.name);
        if (trimmed == text) {
            exact.append({text, name, true});
            continue;
        }
        const QStringList words = wordsOf(QString::fromUtf8(symbol.name) + QLatin1Char(' ') + name + QLatin1Char(' ')
                                          + QString::fromUtf8(symbol.keywords));
        bool all = true;
        bool whole = true;
        for (const QString &token : tokens) {
            bool found = false;
            bool equal = false;
            for (const QString &word : words) {
                if (word.startsWith(token)) {
                    found = true;
                    equal = equal || word == token;
                }
            }
            all = all && found;
            whole = whole && equal;
        }
        if (all)
            (whole ? exact : partial).append({text, name, whole});
    }
    return (exact + partial).mid(0, limit);
}

QString symbolCodes(const QString &text)
{
    QStringList codes;
    for (const char32_t code : text.toUcs4())
        codes.append(QStringLiteral("U+") + QString::number(uint(code), 16).toUpper().rightJustified(4, QLatin1Char('0')));
    return codes.join(QLatin1Char(' '));
}
