// SPDX-License-Identifier: GPL-3.0-or-later
// A calculator key: a rounded tile with a label or a Lucide glyph. Digits
// sit on cards, functions on the pane color, operators in the accent tint
// and equals in the accent.
#pragma once

#include <QAbstractButton>

class CalcKey : public QAbstractButton
{
    Q_OBJECT
public:
    enum class Kind { Digit, Function, Operator, Equals };
    CalcKey(const QString &label, Kind kind, QWidget *parent = nullptr);
    void setGlyph(const QString &glyph);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Kind m_kind;
    QString m_glyph;
};
