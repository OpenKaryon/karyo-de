// SPDX-License-Identifier: GPL-3.0-or-later
#include "labpages.h"

#include "engine.h"

#include <karyo/ui/navigation.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <cmath>

namespace Ui = Karyo::Ui;

namespace {

QLabel *mutedLabel(const QString &text, qreal size)
{
    auto *label = new QLabel(text);
    label->setFont(Ui::bodyFont(size));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, Ui::Theme::current().muted);
    label->setPalette(palette);
    return label;
}

// A value in base units with the unit that suits it: 0.0099 l -> 9.9 ml.
QString withUnit(Lab::Family family, double base)
{
    const Lab::Pick pick = Lab::bestUnit(family, base);
    return Calc::format(pick.value, 4) + QLatin1Char(' ') + Lab::units(family).at(pick.unit).name;
}

bool positive(const LabField *field)
{
    const double value = field->base();
    return std::isfinite(value) && value > 0;
}

} // namespace

LabField::LabField(const QString &label, const QList<Lab::Family> &families, int defaultUnit, const QString &value,
                   QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit)
    , m_unit(new QComboBox)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);
    v->addWidget(mutedLabel(label, 13));
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    m_edit->setFont(Ui::bodyFont(17));
    m_edit->setMinimumHeight(42);
    m_edit->setText(value);
    for (Lab::Family family : families) {
        const QList<Lab::Unit> &list = Lab::units(family);
        for (int i = 0; i < list.size(); ++i) {
            m_units.append({family, i});
            m_unit->addItem(list.at(i).name);
        }
    }
    m_unit->setCurrentIndex(defaultUnit);
    m_unit->setMinimumWidth(104);
    m_unit->setMinimumHeight(42);
    m_unit->setFont(Ui::bodyFont(14));
    row->addWidget(m_edit, 1);
    row->addWidget(m_unit);
    v->addLayout(row);
    connect(m_edit, &QLineEdit::textEdited, this, &LabField::changed);
    connect(m_unit, &QComboBox::activated, this, [this] {
        if (m_result)
            m_unitPicked = true;
        Q_EMIT changed();
    });
}

bool LabField::hasValue() const { return !m_edit->text().trimmed().isEmpty(); }

double LabField::base() const
{
    const Calc::Result result = Calc::evaluate(m_edit->text());
    if (!result.ok)
        return NAN;
    const auto unit = m_units.value(m_unit->currentIndex());
    return result.value * Lab::units(unit.first).at(unit.second).factor;
}

Lab::Family LabField::family() const { return m_units.value(m_unit->currentIndex()).first; }

QString LabField::unitName() const { return m_unit->currentText(); }

QString LabField::text() const { return m_edit->text().trimmed() + QLatin1Char(' ') + unitName(); }

void LabField::setValue(const QString &value) { m_edit->setText(value); }

void LabField::setResult(bool result)
{
    m_result = result;
    m_unitPicked = false;
    m_edit->setReadOnly(result);
    m_edit->setFocusPolicy(result ? Qt::NoFocus : Qt::StrongFocus);
    const Ui::Theme &t = Ui::Theme::current();
    m_edit->setFont(Ui::bodyFont(17, result ? QFont::DemiBold : QFont::Normal));
    const QColor fill = t.tint1;
    m_edit->setStyleSheet(
        result ? QStringLiteral("QLineEdit { background: rgba(%1, %2, %3, %4); color: %5; border: none; "
                                "border-radius: 14px; padding: 0 12px; }")
                     .arg(fill.red())
                     .arg(fill.green())
                     .arg(fill.blue())
                     .arg(fill.alpha())
                     .arg(t.ink1.name())
               : QString());
}

void LabField::showResult(double base)
{
    Lab::Family family = this->family();
    if (!m_unitPicked) {
        const Lab::Pick pick = Lab::bestUnit(family, base);
        for (int i = 0; i < m_units.size(); ++i) {
            if (m_units.at(i).first == family && m_units.at(i).second == pick.unit) {
                m_unit->setCurrentIndex(i);
                break;
            }
        }
    }
    const auto unit = m_units.value(m_unit->currentIndex());
    m_edit->setText(Calc::format(base / Lab::units(unit.first).at(unit.second).factor, 4));
}

void LabField::clearResult() { m_edit->setText(QStringLiteral("—")); }

LabPage::LabPage(const QString &title, const QString &intro, QWidget *parent)
    : QWidget(parent)
    , m_layout(new QVBoxLayout(this))
    , m_grid(new QGridLayout)
    , m_targetRow(new QWidget)
    , m_recipe(new QLabel)
    , m_copy(nullptr)
{
    const Ui::Theme &t = Ui::Theme::current();
    m_layout->setContentsMargins(28, 22, 28, 22);
    m_layout->setSpacing(8);
    auto *heading = new QLabel(title);
    heading->setFont(Ui::bodyFont(22, QFont::DemiBold));
    m_layout->addWidget(heading);
    auto *introLabel = mutedLabel(intro, 13.5);
    introLabel->setWordWrap(true);
    m_layout->addWidget(introLabel);
    m_layout->addSpacing(10);
    m_layout->addWidget(m_targetRow);
    m_layout->addSpacing(6);
    m_grid->setHorizontalSpacing(18);
    m_grid->setVerticalSpacing(14);
    m_layout->addLayout(m_grid);
    m_layout->addSpacing(12);

    // The answer in words, on a tinted card, with a copy button.
    auto *card = new Ui::InsetPanel;
    card->setFill(t.pane);
    card->setRadius(18);
    auto *cardRow = new QHBoxLayout(card);
    cardRow->setContentsMargins(18, 12, 10, 12);
    m_recipe->setFont(Ui::bodyFont(15));
    m_recipe->setWordWrap(true);
    m_recipe->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_recipe->setMinimumHeight(44);
    cardRow->addWidget(m_recipe, 1);
    m_copy = new Ui::IconButton(QStringLiteral("copy"), card);
    m_copy->setHint(tr("Copy the result"));
    connect(m_copy, &QAbstractButton::clicked, this, [this] {
        if (m_target)
            QGuiApplication::clipboard()->setText(m_target->text());
    });
    cardRow->addWidget(m_copy, 0, Qt::AlignTop);
    m_layout->addWidget(card);
}

LabField *LabPage::addField(const QString &label, const QList<Lab::Family> &families, int defaultUnit,
                            const QString &value)
{
    auto *field = new LabField(label, families, defaultUnit, value);
    const int index = int(m_fields.size());
    m_grid->addWidget(field, index / 2, index % 2);
    m_fields.append(field);
    connect(field, &LabField::changed, this, &LabPage::recompute);
    return field;
}

void LabPage::setTargets(const QList<QPair<QString, LabField *>> &targets)
{
    auto *row = new QHBoxLayout(m_targetRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    auto *label = mutedLabel(tr("Calculate"), 13.5);
    row->addWidget(label);
    row->addSpacing(8);
    for (const auto &target : targets) {
        auto *button = new Ui::PillButton(QString(), target.first, m_targetRow);
        LabField *field = target.second;
        connect(button, &QAbstractButton::clicked, this, [this, field] { selectTarget(field); });
        row->addWidget(button);
        m_targetButtons.append({button, field});
    }
    row->addStretch(1);
    selectTarget(targets.first().second);
}

void LabPage::selectTarget(LabField *field)
{
    if (m_target == field)
        return;
    if (m_target) {
        m_target->setResult(false);
        if (!positive(m_target))
            m_target->setValue(QString());
    }
    m_target = field;
    field->setResult(true);
    for (const auto &button : std::as_const(m_targetButtons)) {
        button.first->setKind(button.second == field ? Ui::PillButton::Kind::Tint : Ui::PillButton::Kind::Ghost);
        button.first->setActive(button.second == field);
    }
    recompute();
}

void LabPage::recompute()
{
    if (m_computing || !m_target)
        return;
    m_computing = true;
    compute();
    m_computing = false;
}

void LabPage::setRecipe(const QString &recipe)
{
    QPalette palette = m_recipe->palette();
    palette.setColor(QPalette::WindowText, Ui::Theme::current().text);
    m_recipe->setPalette(palette);
    m_recipe->setText(recipe);
    m_copy->setEnabled(true);
}

void LabPage::showProblem(const QString &problem)
{
    m_target->clearResult();
    QPalette palette = m_recipe->palette();
    palette.setColor(QPalette::WindowText, Ui::Theme::current().muted);
    m_recipe->setPalette(palette);
    m_recipe->setText(problem);
    m_copy->setEnabled(false);
}

MolarityPage::MolarityPage(QWidget *parent)
    : LabPage(tr("Molarity"),
              tr("How much to weigh out for a solution, or what concentration you have. Pick what to calculate; "
                 "the other fields are what you know."),
              parent)
{
    using F = Lab::Family;
    m_mass = addField(tr("Mass"), {F::Mass}, 0);
    m_molar = addField(tr("Concentration"), {F::Molar}, 0, QStringLiteral("1"));
    m_volume = addField(tr("Volume"), {F::Volume}, 1, QStringLiteral("100"));
    m_weight = addField(tr("Molecular weight"), {F::Weight}, 0, QStringLiteral("58.44"));
    setTargets({{tr("Mass"), m_mass}, {tr("Concentration"), m_molar}, {tr("Volume"), m_volume}});
    m_layout->addStretch(1);
}

void MolarityPage::compute()
{
    for (LabField *field : {m_mass, m_molar, m_volume, m_weight}) {
        if (field != target() && !positive(field)) {
            showProblem(tr("Fill in the other fields with positive numbers."));
            return;
        }
    }
    if (target() == m_mass)
        m_mass->showResult(Lab::mass(m_molar->base(), m_volume->base(), m_weight->base()));
    else if (target() == m_molar)
        m_molar->showResult(Lab::molarConcentration(m_mass->base(), m_volume->base(), m_weight->base()));
    else
        m_volume->showResult(Lab::volume(m_mass->base(), m_molar->base(), m_weight->base()));
    setRecipe(tr("Dissolve %1 and add solvent up to %2: a %3 solution.")
                  .arg(m_mass->text(), m_volume->text(), m_molar->text()));
}

DilutionPage::DilutionPage(QWidget *parent)
    : LabPage(tr("Dilution"),
              tr("C₁·V₁ = C₂·V₂: how much stock goes into a working solution. Concentrations can be molar, by "
                 "mass or a fold (10×)."),
              parent)
{
    using F = Lab::Family;
    const QList<F> concentrations = {F::Molar, F::MassConcentration, F::Fold};
    m_c1 = addField(tr("Stock concentration (C₁)"), concentrations, 1, QStringLiteral("10"));
    m_v1 = addField(tr("Stock volume (V₁)"), {F::Volume}, 2);
    m_c2 = addField(tr("Final concentration (C₂)"), concentrations, 2, QStringLiteral("100"));
    m_v2 = addField(tr("Final volume (V₂)"), {F::Volume}, 1, QStringLiteral("1"));
    setTargets({{tr("Stock volume"), m_v1},
                {tr("Final concentration"), m_c2},
                {tr("Final volume"), m_v2},
                {tr("Stock concentration"), m_c1}});
    m_layout->addStretch(1);
}

void DilutionPage::compute()
{
    if (m_c1->family() != m_c2->family()) {
        showProblem(tr("Give both concentrations in units of one kind: molar, by mass or a fold."));
        return;
    }
    for (LabField *field : {m_c1, m_v1, m_c2, m_v2}) {
        if (field != target() && !positive(field)) {
            showProblem(tr("Fill in the other fields with positive numbers."));
            return;
        }
    }
    const double c1 = m_c1->base(), v1 = m_v1->base(), c2 = m_c2->base(), v2 = m_v2->base();
    double value = 0;
    Lab::Dilution unknown = Lab::Dilution::V1;
    if (target() == m_c1)
        unknown = Lab::Dilution::C1;
    else if (target() == m_c2)
        unknown = Lab::Dilution::C2;
    else if (target() == m_v2)
        unknown = Lab::Dilution::V2;
    value = Lab::dilute(unknown, c1, v1, c2, v2);
    const double stock = unknown == Lab::Dilution::C1 ? value : c1;
    const double finalConcentration = unknown == Lab::Dilution::C2 ? value : c2;
    if (finalConcentration > stock * (1 + 1e-12)) {
        showProblem(tr("The final concentration can't be above the stock's."));
        return;
    }
    target()->showResult(value);
    const double stockVolume = unknown == Lab::Dilution::V1 ? value : v1;
    const double totalVolume = unknown == Lab::Dilution::V2 ? value : v2;
    setRecipe(tr("Add %1 of the %2 stock to %3 of diluent: %4 at %5.")
                  .arg(m_v1->text(), m_c1->text(), withUnit(Lab::Family::Volume, totalVolume - stockVolume),
                       m_v2->text(), m_c2->text()));
}

NucleicPage::NucleicPage(QWidget *parent)
    : LabPage(tr("Nucleic acids"),
              tr("Mass and molar concentration of DNA or RNA, for library pooling and ligations. Molecular weights "
                 "follow NEB."),
              parent)
    , m_strand(new QComboBox)
    , m_a260(new QLineEdit)
    , m_dilution(new QLineEdit)
    , m_fromA260(new QLabel)
{
    using F = Lab::Family;
    // The molecule above the fields.
    auto *strandRow = new QHBoxLayout;
    strandRow->addWidget(mutedLabel(tr("Molecule"), 13.5));
    strandRow->addSpacing(8);
    m_strand->addItems({QStringLiteral("dsDNA"), QStringLiteral("ssDNA"), QStringLiteral("RNA")});
    m_strand->setMinimumHeight(38);
    m_strand->setMinimumWidth(130);
    m_strand->setFont(Ui::bodyFont(14));
    strandRow->addWidget(m_strand);
    strandRow->addStretch(1);
    m_layout->insertLayout(3, strandRow);

    m_mass = addField(tr("Mass concentration"), {F::MassConcentration}, 2, QStringLiteral("50"));
    m_length = addField(tr("Length"), {F::Length}, 0, QStringLiteral("500"));
    m_molar = addField(tr("Molar concentration"), {F::Molar}, 3);
    setTargets({{tr("Molar concentration"), m_molar}, {tr("Mass concentration"), m_mass}});
    connect(m_strand, &QComboBox::currentIndexChanged, this, [this] {
        // RNA and single strands are counted in nucleotides.
        m_length->unitBox()->setItemText(0, strand() == Lab::Strand::DoubleDna ? QStringLiteral("bp")
                                                                                : QStringLiteral("nt"));
        recompute();
        updateAbsorbance();
    });

    // From a NanoDrop reading.
    m_layout->addSpacing(18);
    auto *heading = new QLabel(tr("From absorbance"));
    heading->setFont(Ui::bodyFont(16, QFont::DemiBold));
    m_layout->addWidget(heading);
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    row->addWidget(mutedLabel(QStringLiteral("A260"), 13.5));
    m_a260->setFixedWidth(110);
    m_a260->setMinimumHeight(38);
    m_a260->setFont(Ui::bodyFont(16));
    row->addWidget(m_a260);
    row->addSpacing(8);
    row->addWidget(mutedLabel(tr("Dilution factor"), 13.5));
    m_dilution->setFixedWidth(80);
    m_dilution->setMinimumHeight(38);
    m_dilution->setFont(Ui::bodyFont(16));
    m_dilution->setText(QStringLiteral("1"));
    row->addWidget(m_dilution);
    row->addSpacing(8);
    m_fromA260->setFont(Ui::bodyFont(16, QFont::DemiBold));
    row->addWidget(m_fromA260);
    auto *use = new Ui::PillButton(QString(), tr("Use"), this);
    use->setKind(Ui::PillButton::Kind::Secondary);
    use->setToolTip(tr("Put it in the mass concentration"));
    connect(use, &QAbstractButton::clicked, this, [this] {
        if (m_a260Result <= 0)
            return;
        if (m_mass->isResult())
            selectTarget(m_molar);
        m_mass->unitBox()->setCurrentIndex(2);  // ng/µl
        m_mass->setValue(Calc::format(m_a260Result * 1e3, 4));
        recompute();
    });
    row->addWidget(use);
    row->addStretch(1);
    m_layout->addLayout(row);
    m_layout->addStretch(1);
    connect(m_a260, &QLineEdit::textEdited, this, &NucleicPage::updateAbsorbance);
    connect(m_dilution, &QLineEdit::textEdited, this, &NucleicPage::updateAbsorbance);
    updateAbsorbance();
}

Lab::Strand NucleicPage::strand() const
{
    return m_strand->currentIndex() == 0   ? Lab::Strand::DoubleDna
           : m_strand->currentIndex() == 1 ? Lab::Strand::SingleDna
                                           : Lab::Strand::Rna;
}

void NucleicPage::compute()
{
    for (LabField *field : {m_mass, m_length, m_molar}) {
        if (field != target() && !positive(field)) {
            showProblem(tr("Fill in the other fields with positive numbers."));
            return;
        }
    }
    const double length = m_length->base();
    if (target() == m_molar)
        m_molar->showResult(Lab::nucleicMolar(strand(), m_mass->base(), length));
    else
        m_mass->showResult(Lab::nucleicMass(strand(), m_molar->base(), length));
    setRecipe(tr("%1 of %2 %3 is %4; it weighs %5 g/mol.")
                  .arg(m_mass->text(), m_length->text(), m_strand->currentText(), m_molar->text(),
                       Calc::format(std::round(Lab::nucleicWeight(strand(), length)), 9)));
}

void NucleicPage::updateAbsorbance()
{
    const Calc::Result a260 = Calc::evaluate(m_a260->text());
    const Calc::Result dilution = Calc::evaluate(m_dilution->text());
    if (!a260.ok || !dilution.ok || a260.value <= 0 || dilution.value <= 0) {
        m_a260Result = 0;
        m_fromA260->setText(QStringLiteral("—"));
        return;
    }
    m_a260Result = Lab::fromAbsorbance(strand(), a260.value, dilution.value);
    m_fromA260->setText(QStringLiteral("= ") + withUnit(Lab::Family::MassConcentration, m_a260Result));
}
