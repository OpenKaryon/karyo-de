// SPDX-License-Identifier: GPL-3.0-or-later
// Starts the lock screen (karyo-lock) for Start, and when
// logind asks: "loginctl lock-session" and going to sleep. With
// Desktops/lockOnLeave in shell.conf it also locks when another Karyo
// desktop takes the screen, so coming back asks for the password.
#pragma once

#include <QObject>
#include <QDBusUnixFileDescriptor>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>

class SessionLocker : public QObject
{
    Q_OBJECT
public:
    explicit SessionLocker(QObject *parent = nullptr);

public Q_SLOTS:
    static void lock();

private Q_SLOTS:
    void onPrepareForSleep(bool start);
    void onSessionChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
private:
    void acquireSleepDelay();
    void startLock();
    QProcess m_process;
    QDBusUnixFileDescriptor m_sleepDelay;
    QByteArray m_output;
    bool m_ready = false;
    bool m_preparingForSleep = false;
};
