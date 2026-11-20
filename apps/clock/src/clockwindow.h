// SPDX-License-Identifier: GPL-3.0-or-later
// Karyo Clock: Timers and Stopwatch. Closing the window while something
// runs keeps Clock going with an icon in the tray, so a timer still rings;
// a second start, the tray icon or a notification brings the window back.
#pragma once

#include <karyo/ui/window.h>

#include <QHash>
#include <QTimer>

class Alarm;
class QSystemTrayIcon;
class StopwatchPage;
class TimerModel;
class TimersPage;

namespace Karyo::Ui {
class NavList;
}

class ClockWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit ClockWindow(QWidget *parent = nullptr);
    void bringBack();
    void showStopwatch();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    bool busy() const;
    void onFinished(const QString &id);
    void stopAlarm(const QString &id, bool resetTimer);
    void updateTray();

    TimerModel *m_model;
    TimersPage *m_timers;
    StopwatchPage *m_stopwatch;
    Karyo::Ui::NavList *m_nav;
    int m_stopwatchIndex = 1;
    QHash<QString, Alarm *> m_alarms;
    QSystemTrayIcon *m_tray = nullptr;
    QTimer m_tick;
    bool m_toldBackground = false;
};

// org.openkaryon.Clock on the session bus: a second start shows the window.
class ClockService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.openkaryon.Clock")
public:
    explicit ClockService(ClockWindow *window);

public Q_SLOTS:
    Q_SCRIPTABLE void Show(const QString &mode);

private:
    ClockWindow *m_window;
};
