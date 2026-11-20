// SPDX-License-Identifier: GPL-3.0-or-later
// The named timers. A running timer keeps its deadline on the wall clock,
// so it goes on counting while Clock is closed or after a restart, and one
// that ran out meanwhile reports as finished on the next tick. They are
// kept in ~/.config/openkaryon/clock-timers.json.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class TimerModel : public QObject
{
    Q_OBJECT
public:
    enum class State { Ready, Running, Paused, Done };
    struct Timer
    {
        QString id;
        QString name;
        qint64 duration = 0;   // ms
        State state = State::Ready;
        qint64 deadline = 0;   // epoch ms, while running
        qint64 left = 0;       // ms, while paused
        qint64 finishedAt = 0; // epoch ms, once done
    };

    // `path` empty: nothing is read or written (tests); `now` for tests.
    explicit TimerModel(const QString &path, std::function<qint64()> now = {}, QObject *parent = nullptr);

    static QString defaultPath();
    // The first start's timers, for the bench.
    void addSamples();

    const QList<Timer> &timers() const { return m_timers; }
    int indexOf(const QString &id) const;
    qint64 remaining(int index) const;  // ms; 0 once done
    bool anyRunning() const;

    int add(const QString &name, qint64 duration);
    void remove(int index);
    void edit(int index, const QString &name, qint64 duration);
    void start(int index);
    void pause(int index);
    void reset(int index);
    void addTime(int index, qint64 ms);

    // Finds timers that ran out; called a few times a second.
    void tick();

Q_SIGNALS:
    void changed();
    void finished(const QString &id);

private:
    void load();
    void save() const;
    void touch();

    QString m_path;
    std::function<qint64()> m_now;
    QList<Timer> m_timers;
};
