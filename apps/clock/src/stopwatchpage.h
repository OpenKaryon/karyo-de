// SPDX-License-Identifier: GPL-3.0-or-later
// Stopwatch: the time to the hundredth, laps with the fastest and slowest
// marked. Space starts or pauses, L takes a lap, R resets.
#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QWidget>

class LapList;
class QLabel;
class QTimer;

namespace Karyo::Ui {
class PillButton;
}

class StopwatchPage : public QWidget
{
    Q_OBJECT
public:
    explicit StopwatchPage(QWidget *parent = nullptr);
    bool isRunning() const { return m_running; }

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    qint64 elapsed() const;
    void toggle();
    void lap();
    void reset();
    void refresh();

    QLabel *m_time;
    Karyo::Ui::PillButton *m_start;
    Karyo::Ui::PillButton *m_lap;
    Karyo::Ui::PillButton *m_reset;
    LapList *m_laps;
    QTimer *m_tick;
    QElapsedTimer m_clock;
    qint64 m_before = 0;  // ms counted before the last start
    bool m_running = false;
    QList<qint64> m_lapTotals;
};
