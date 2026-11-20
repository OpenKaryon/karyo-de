// SPDX-License-Identifier: GPL-3.0-or-later
// Starts the lock screen (openkaryon-signin lock) for Start, and when
// logind asks: "loginctl lock-session" and going to sleep. With
// Desktops/lockOnLeave in shell.conf it also locks when another Karyo
// desktop takes the screen, so coming back asks for the password.
#pragma once

#include <QObject>
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
};
