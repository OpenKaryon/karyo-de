// SPDX-License-Identifier: GPL-3.0-or-later
// openkaryon-signin lock: the lock screen. The session stays locked by the
// compositor (ext-session-lock-v1) until PAM accepts the user's password.
#include "pamcheck.h"
#include "sessionlock.h"
#include "signinview.h"
#include "signin.h"

#include <karyo/ui/password.h>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QScreen>
#include <QWindow>

#include <cstdio>

#include <pwd.h>
#include <unistd.h>

namespace {

SignInView *createView(QScreen *screen, bool decorationOnly)
{
    auto *view = new SignInView(decorationOnly);
    view->setAttribute(Qt::WA_DeleteOnClose, false);
    view->setScreen(screen);
    view->resize(screen->size());
    view->winId();
    return view;
}

} // namespace

int runLock()
{
    QApplication::setQuitOnLastWindowClosed(false);
    const passwd *pw = getpwuid(getuid());
    if (!pw) {
        std::fputs("karyo-lock: the user is unknown\n", stderr);
        return 1;
    }
    const QString login = QString::fromLocal8Bit(pw->pw_name);
    const QString fullName = SignIn::fullName(pw);

    QScreen *primary = QGuiApplication::primaryScreen();
    if (!primary)
        return 1;
    auto *main = createView(primary, false);
    QList<SignInView *> views{main};
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen != primary)
            views.append(createView(screen, true));
    }

    SessionLock::Lock *lock = SessionLock::Lock::create(main->windowHandle(), qApp);
    if (!lock) {
        std::fputs("karyo-lock: the compositor cannot lock the session (ext-session-lock-v1)\n", stderr);
        return 1;
    }
    const auto bus = QDBusConnection::systemBus();
    QDBusMessage lookup = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("GetSessionByPID"));
    lookup << uint(getpid());
    const QDBusReply<QDBusObjectPath> session = bus.call(lookup);
    const auto setLockedHint = [bus, session](bool locked) {
        if (!session.isValid())
            return;
        auto message = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.login1"), session.value().path(),
            QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("SetLockedHint"));
        message << locked;
        bus.call(message, QDBus::Block, 1000);
    };
    QObject::connect(lock, &SessionLock::Lock::locked, qApp, [setLockedHint] { setLockedHint(true); });
    QObject::connect(lock, &SessionLock::Lock::finished, qApp, [] { QCoreApplication::exit(1); });
    for (SignInView *view : std::as_const(views)) {
        lock->attach(view->windowHandle());
        view->show();
    }
    QObject::connect(qApp, &QGuiApplication::screenAdded, qApp, [lock](QScreen *screen) {
        SignInView *view = createView(screen, true);
        lock->attach(view->windowHandle());
        view->show();
    });

    main->setUser(fullName.isEmpty() ? login : fullName, fullName.isEmpty() ? QString() : login);
    Karyo::Ui::PasswordField *password = main->password();
    password->setPlaceholder(SignInText::tr("Password"));
    password->setFocus();

    auto *check = new PamCheck(QStringLiteral("karyo-lock"), qApp);
    QObject::connect(password, &Karyo::Ui::PasswordField::submitted, qApp, [=] {
        if (password->text().isEmpty())
            return;
        password->setBusy(true);
        main->showMessage(QString());
        const QString secret = password->text();
        password->clear();
        check->check(login, secret);
    });
    QObject::connect(check, &PamCheck::finished, qApp, [=](bool ok) {
        if (ok) {
            auto unlock = [lock, setLockedHint] {
                lock->unlock();
                setLockedHint(false);
                QCoreApplication::quit();
            };
            // A very quick answer may come before the compositor confirms the lock.
            if (lock->isLocked())
                unlock();
            else
                QObject::connect(lock, &SessionLock::Lock::locked, qApp, unlock);
            return;
        }
        password->setBusy(false);
        password->clear();
        password->showError();
        password->setFocus();
        main->showMessage(SignInText::tr("That password didn't work. Try again."));
    });
    return QApplication::exec();
}
