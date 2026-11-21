// SPDX-License-Identifier: GPL-3.0-or-later
#include "lockviews.h"
#include "pamcheck.h"
#include "sessionlock.h"
#include "signin.h"
#include "signinview.h"
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <karyo/ui/password.h>
#include <utility>

LockViews::LockViews(
    SessionLock::Lock *lock, QString login, QString fullName, std::function<void()> authenticated)
    : m_lock(lock)
    , m_check(new PamCheck(QStringLiteral("karyo-lock"), this))
    , m_login(std::move(login))
    , m_fullName(std::move(fullName))
    , m_authenticated(std::move(authenticated))
{
    connect(m_check, &PamCheck::finished, this, [this](bool ok) { checked(ok); });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] { scheduleReconcile(); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this,
        [this](QScreen *screen)
        {
            removeScreen(screen);
            scheduleReconcile();
        });
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this] { scheduleReconcile(); });
    reconcile();
}

LockViews::~LockViews() { qDeleteAll(m_views); }

void LockViews::removeScreen(QScreen *screen)
{
    if (auto *view = m_views.take(screen))
    {
        view->password()->clear();
        view->hide();
        view->deleteLater();
    }
}

void LockViews::scheduleReconcile()
{
    if (m_scheduled)
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this,
        [this]
        {
            m_scheduled = false;
            reconcile();
        });
}

void LockViews::reconcile()
{
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : m_views.keys())
    {
        if (!screens.contains(screen) || !SessionLock::Lock::outputAvailable(screen))
            removeScreen(screen);
    }
    for (QScreen *screen : screens)
    {
        // A Qt placeholder has no wl_output and cannot receive a lock surface.
        // Keep the compositor lock alive until an actual output appears.
        if (m_views.contains(screen) || !SessionLock::Lock::outputAvailable(screen))
            continue;
        auto *view = new SignInView(false);
        view->setAttribute(Qt::WA_DeleteOnClose, false);
        view->setScreen(screen);
        view->resize(screen->size());
        view->winId();
        if (!m_lock->attach(view->windowHandle()))
        {
            delete view;
            continue;
        }
        view->setUser(
            m_fullName.isEmpty() ? m_login : m_fullName, m_fullName.isEmpty() ? QString() : m_login);
        view->showMessage(m_message);
        auto *password = view->password();
        password->setPlaceholder(SignInText::tr("Password"));
        password->setBusy(m_checking);
        connect(password, &Karyo::Ui::PasswordField::submitted, this, [this, view] { submit(view); });
        m_views.insert(screen, view);
        view->show();
        password->setFocus();
    }
}

void LockViews::submit(SignInView *view)
{
    if (m_checking || view->password()->text().isEmpty())
        return;
    const QString secret = view->password()->text();
    m_checking = true;
    m_submitted = view;
    m_message.clear();
    for (auto *current : std::as_const(m_views))
    {
        current->showMessage(QString());
        current->password()->clear();
        current->password()->setBusy(true);
    }
    m_check->check(m_login, secret);
}

void LockViews::checked(bool accepted)
{
    if (accepted)
    {
        m_authenticated();
        return;
    }
    m_checking = false;
    m_message = SignInText::tr("That password didn't work. Try again.");
    for (auto *view : std::as_const(m_views))
    {
        view->showMessage(m_message);
        view->password()->setBusy(false);
        view->password()->clear();
        view->password()->showError();
    }
    if (m_submitted)
        m_submitted->password()->setFocus();
    else if (!m_views.isEmpty())
        m_views.constBegin().value()->password()->setFocus();
}
