// SPDX-License-Identifier: GPL-3.0-or-later
// openkaryon-signin lock: the lock screen. The session stays locked by the
// compositor (ext-session-lock-v1) until PAM accepts the user's password.
#include "lockviews.h"
#include "sessionlock.h"
#include "signin.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QWindow>

#include <cstdio>

#include <pwd.h>
#include <unistd.h>

int runLock()
{
    QApplication::setQuitOnLastWindowClosed(false);
    const passwd *pw = getpwuid(getuid());
    if (!pw)
    {
        std::fputs("karyo-lock: the user is unknown\n", stderr);
        return 1;
    }
    const QString login = QString::fromLocal8Bit(pw->pw_name);
    const QString fullName = SignIn::fullName(pw);

    // Acquire the lock through a hidden connection window, independently of
    // output lifetime. Qt may have only a placeholder while this VT is inactive.
    QWindow connection;
    connection.create();
    SessionLock::Lock *lock = SessionLock::Lock::create(&connection, qApp);
    if (!lock)
    {
        std::fputs("karyo-lock: the compositor cannot lock the session (ext-session-lock-v1)\n", stderr);
        return 1;
    }
    const auto bus = QDBusConnection::systemBus();
    QDBusMessage lookup = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("GetSessionByPID"));
    lookup << uint(getpid());
    const QDBusReply<QDBusObjectPath> session = bus.call(lookup);
    const auto setLockedHint = [bus, session](bool locked)
    {
        if (!session.isValid())
            return;
        auto message
            = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), session.value().path(),
                QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("SetLockedHint"));
        message << locked;
        bus.call(message, QDBus::Block, 1000);
    };
    QObject::connect(lock, &SessionLock::Lock::locked, qApp,
        [setLockedHint]
        {
            setLockedHint(true);
            std::fputs("KARYO_LOCKED\n", stdout);
            std::fflush(stdout);
        });
    QObject::connect(lock, &SessionLock::Lock::finished, qApp, [] { QCoreApplication::exit(1); });
    LockViews views(lock, login, fullName,
        [lock, setLockedHint]
        {
            auto unlock = [lock, setLockedHint]
            {
                lock->unlock();
                setLockedHint(false);
                QCoreApplication::quit();
            };
            // A very quick PAM answer may precede the compositor's lock event.
            if (lock->isLocked())
                unlock();
            else
                QObject::connect(lock, &SessionLock::Lock::locked, qApp, unlock);
        });
    return QApplication::exec();
}
