// SPDX-License-Identifier: GPL-3.0-or-later
// The Lab modes: Molarity (mass, concentration, volume, molecular weight),
// Dilution (C1·V1 = C2·V2) and Nucleic acids (ng/µl and nM for dsDNA, ssDNA
// and RNA, and the concentration from A260). One field is the result, the
// others are inputs; fields take expressions too (2*58.44).
#pragma once

#include "lab.h"

#include <QWidget>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace Karyo::Ui {
class IconButton;
class PillButton;
}

// A labelled number with a unit menu.
class LabField : public QWidget
{
    Q_OBJECT
public:
    LabField(const QString &label, const QList<Lab::Family> &families, int defaultUnit,
             const QString &value, QWidget *parent = nullptr);

    bool hasValue() const;
    double base() const;  // the value in base units; NaN when it is not a number
    Lab::Family family() const;
    QString unitName() const;
    QString text() const;  // the value and its unit, as shown
    void setValue(const QString &value);

    // The result field: read-only, tinted, its unit picked to fit the value
    // until the reader picks one.
    void setResult(bool result);
    bool isResult() const { return m_result; }
    void showResult(double base);
    void clearResult();

    QComboBox *unitBox() const { return m_unit; }

Q_SIGNALS:
    void changed();

private:
    QLineEdit *m_edit;
    QComboBox *m_unit;
    QList<QPair<Lab::Family, int>> m_units;
    bool m_result = false;
    bool m_unitPicked = false;
};

class LabPage : public QWidget
{
    Q_OBJECT
public:
    LabPage(const QString &title, const QString &intro, QWidget *parent = nullptr);

protected:
    LabField *addField(const QString &label, const QList<Lab::Family> &families, int defaultUnit,
                       const QString &value = QString());
    // The fields one can solve for; the first is selected.
    void setTargets(const QList<QPair<QString, LabField *>> &targets);
    LabField *target() const { return m_target; }
    void selectTarget(LabField *field);

    virtual void compute() = 0;
    // After target()->showResult(): how to make it, or what the result means.
    void setRecipe(const QString &recipe);
    void showProblem(const QString &problem);
    void recompute();

    QVBoxLayout *m_layout;
    QGridLayout *m_grid;
    QList<LabField *> m_fields;

private:
    QList<QPair<Karyo::Ui::PillButton *, LabField *>> m_targetButtons;
    LabField *m_target = nullptr;
    QWidget *m_targetRow;
    QLabel *m_recipe;
    Karyo::Ui::IconButton *m_copy;
    bool m_computing = false;
};

class MolarityPage : public LabPage
{
    Q_OBJECT
public:
    explicit MolarityPage(QWidget *parent = nullptr);

protected:
    void compute() override;

private:
    LabField *m_mass;
    LabField *m_molar;
    LabField *m_volume;
    LabField *m_weight;
};

class DilutionPage : public LabPage
{
    Q_OBJECT
public:
    explicit DilutionPage(QWidget *parent = nullptr);

protected:
    void compute() override;

private:
    LabField *m_c1;
    LabField *m_v1;
    LabField *m_c2;
    LabField *m_v2;
};

class NucleicPage : public LabPage
{
    Q_OBJECT
public:
    explicit NucleicPage(QWidget *parent = nullptr);

protected:
    void compute() override;

private:
    Lab::Strand strand() const;
    void updateAbsorbance();

    QComboBox *m_strand;
    LabField *m_mass;
    LabField *m_length;
    LabField *m_molar;
    QLineEdit *m_a260;
    QLineEdit *m_dilution;
    QLabel *m_fromA260;
    double m_a260Result = 0;
};
