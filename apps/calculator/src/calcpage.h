// SPDX-License-Identifier: GPL-3.0-or-later
// The Standard and Scientific modes: an editable expression with its result
// shown as you type, a keypad (Scientific adds functions, π, e, parentheses
// and degrees or radians) and the history the two modes share.
#pragma once

#include "engine.h"

#include <QList>
#include <QWidget>

class CalcKey;
class QLabel;
class QLineEdit;
class QScrollArea;

// Past calculations, newest first.
class History : public QObject
{
    Q_OBJECT
public:
    struct Entry
    {
        QString expression;
        QString result;
    };
    using QObject::QObject;
    void add(const QString &expression, const QString &result);
    void clear();
    const QList<Entry> &entries() const { return m_entries; }

Q_SIGNALS:
    void changed();

private:
    QList<Entry> m_entries;
};

class HistoryList;

class CalcPage : public QWidget
{
    Q_OBJECT
public:
    CalcPage(bool scientific, History *history, QWidget *parent = nullptr);
    void focusDisplay();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Wrap { Reciprocal, Square, Root, Negate };
    void addKey(int row, int column, CalcKey *key);
    void insert(const QString &text);
    void wrapOperand(Wrap wrap);
    int operandStart(const QString &text) const;
    void evaluate();
    void clearAll();
    void clearEntry();
    void backspace();
    void recall(int index);
    void setAngle(Calc::Angle angle);
    void updatePreview();
    void fitFont();

    bool m_scientific;
    History *m_history;
    QLabel *m_previous;
    QLineEdit *m_edit;
    QLabel *m_preview;
    QWidget *m_keys;
    CalcKey *m_angleKey = nullptr;
    HistoryList *m_historyList;
    QScrollArea *m_historyScroll;
    Calc::Angle m_angle = Calc::Angle::Degrees;
    double m_ans = 0;
    bool m_evaluated = false;
};
