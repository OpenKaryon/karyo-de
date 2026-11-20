// SPDX-License-Identifier: GPL-3.0-or-later
// A finished timer's alarm: a notification that stays until it is answered
// (Restart or dismiss) and the freedesktop alarm sound every few seconds
// for a minute.
#pragma once

#include <QObject>
#include <QTimer>

class QProcess;

class Alarm : public QObject
{
    Q_OBJECT
public:
    // `ring`: play the sound (not for a timer that ran out long ago).
    Alarm(const QString &name, qint64 finishedAt, bool ring, QObject *parent = nullptr);
    ~Alarm() override;

    // Silences it and takes the notification away.
    void stop();

Q_SIGNALS:
    void restartRequested();
    void showRequested();
    void dismissed();

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &action);
    void onClosed(uint id, uint reason);

private:
    void ring();

    uint m_id = 0;
    bool m_stopped = false;
    int m_rings = 0;
    QTimer m_repeat;
    QProcess *m_player = nullptr;
};
