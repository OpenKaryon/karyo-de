// SPDX-License-Identifier: GPL-3.0-or-later
// Timers: a card per named timer with a ring for the time left, start or
// pause, reset and a minute more; a dialog adds or edits one, with the
// bench's usual timers ready to pick.
#pragma once

#include "timers.h"

#include <QDialog>
#include <QHash>
#include <QWidget>

class QGridLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace Karyo::Ui {
class IconButton;
class PillButton;
}

class TimerCard : public QWidget
{
    Q_OBJECT
public:
    TimerCard(TimerModel *model, const QString &id, QWidget *parent = nullptr);
    void refresh();

Q_SIGNALS:
    void stopRequested(const QString &id);  // a finished timer is acknowledged
    void editRequested(const QString &id);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    int index() const { return m_model->indexOf(m_id); }
    void showMenu();

    TimerModel *m_model;
    QString m_id;
    QLabel *m_name;
    Karyo::Ui::PillButton *m_primary;
    Karyo::Ui::IconButton *m_reset;
    Karyo::Ui::PillButton *m_more;
};

class TimerDialog : public QDialog
{
    Q_OBJECT
public:
    // Empty name and zero duration: a new timer, with presets.
    TimerDialog(const QString &name, qint64 duration, QWidget *parent = nullptr);
    QString name() const;
    qint64 duration() const;

private:
    void setDuration(qint64 ms);

    QLineEdit *m_name;
    QSpinBox *m_hours;
    QSpinBox *m_minutes;
    QSpinBox *m_seconds;
};

class TimersPage : public QWidget
{
    Q_OBJECT
public:
    explicit TimersPage(TimerModel *model, QWidget *parent = nullptr);
    void refresh();  // the time on every card
    void addTimer();
    void editTimer(const QString &id);

Q_SIGNALS:
    void stopRequested(const QString &id);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void relayout();

    TimerModel *m_model;
    QWidget *m_grid;
    QLabel *m_empty;
    QHash<QString, TimerCard *> m_cards;
    QStringList m_order;
    int m_columns = 0;
};
