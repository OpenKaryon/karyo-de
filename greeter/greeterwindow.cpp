// SPDX-License-Identifier: GPL-3.0-or-later
#include "greeterwindow.h"
#include <QAbstractButton>
#include <QFontMetrics>
#include <QLineEdit>
#include <karyo/ui/password.h>
#include <karyo/ui/theme.h>
#include <utility>

GreeterWindow::GreeterWindow(Greetd *transport, QList<GreeterUser> users, QList<GreeterSession> sessions,
    const GreeterSelection &selected, const QString &preferencesRoot, QWidget *parent)
    : SignInView(false, parent)
    , m_auth(new GreeterAuthentication(transport, this))
    , m_users(std::move(users))
    , m_sessions(std::move(sessions))
    , m_preferencesRoot(preferencesRoot)
{
    setWindowTitle(tr("Sign in to Karyo"));
    m_auth->setObjectName("greeterAuthentication");
    password()->setObjectName("greeterPassword");
    for (int i = 0; i < m_users.size(); ++i)
        if (m_users.at(i).login == selected.user)
            m_user = i;
    for (int i = 0; i < m_sessions.size(); ++i)
        if (m_sessions.at(i).id == selected.session)
            m_session = i;
    if (m_users.size() > 1)
    {
        m_other = addLink(tr("Other user"));
        m_other->setObjectName("greeterOtherUser");
        connect(m_other, &QAbstractButton::clicked, this, &GreeterWindow::chooseUser);
    }
    if (m_sessions.size() > 1)
    {
        m_sessionLink = addLink(QString());
        m_sessionLink->setObjectName("greeterSession");
        connect(m_sessionLink, &QAbstractButton::clicked, this, &GreeterWindow::chooseSession);
    }
    m_cancel = addLink(tr("Cancel"));
    m_cancel->setObjectName("greeterCancel");
    connect(m_cancel, &QAbstractButton::clicked, this,
        [this]
        {
            if (m_auth->cancel())
            {
                m_cancelled = true;
                m_cancel->setEnabled(false);
                status(tr("Cancelling sign-in…"));
            }
        });
    connect(password(), &Karyo::Ui::PasswordField::submitted, this, &GreeterWindow::submit);
    connect(m_auth, &GreeterAuthentication::stateChanged, this, &GreeterWindow::sync);
    connect(m_auth, &GreeterAuthentication::promptRequested, this,
        [this](const QString &text, bool secret)
        {
            password()->clear();
            password()->lineEdit()->setEchoMode(secret ? QLineEdit::Password : QLineEdit::Normal);
            const auto placeholder = text.endsWith(QLatin1Char(':')) ? text.chopped(1) : text;
            password()->setPlaceholder(placeholder.size() <= 64 ? placeholder : tr("Response"));
            status(text);
            password()->setFocus();
        });
    connect(m_auth, &GreeterAuthentication::message, this,
        [this](const QString &text, bool error)
        {
            status(text);
            if (error)
                password()->showError();
            if (m_auth->state() == GreeterAuthentication::State::Failed)
                Q_EMIT connectionFailed(text);
        });
    connect(m_auth, &GreeterAuthentication::sessionReady, this,
        [this](const QString &user)
        {
            const auto session = m_sessions.at(m_session).id;
            QString error;
            if (!saveGreeterSelection(user, session, &error, m_preferencesRoot))
                Q_EMIT preferenceError(error); // Preferences must not veto an authorized session.
            Q_EMIT accepted(user, session);
        });
    showUser();
    showSession();
    status(selected.error);
    if (selected.error.isEmpty())
    {
        if (m_users.isEmpty())
            status(tr("There are no main login accounts."));
        else if (m_sessions.isEmpty())
            status(tr("There are no usable Wayland sessions."));
    }
    sync();
}

void GreeterWindow::status(const QString &text)
{
    m_status = text;
    showMessage(text);
}

void GreeterWindow::sync()
{
    using State = GreeterAuthentication::State;
    const auto state = m_auth->state();
    const bool idle = state == State::Idle;
    if (idle)
    {
        if (m_cancelled)
        {
            m_status.clear();
            m_cancelled = false;
        }
        password()->clear();
        password()->lineEdit()->setEchoMode(QLineEdit::Password);
        password()->setPlaceholder(tr("Password"));
    }
    if (state == State::Failed)
        password()->clear();
    const bool canSubmit = (idle || state == State::Prompt) && !m_users.isEmpty() && !m_sessions.isEmpty();
    password()->setBusy(!canSubmit);
    if (m_other)
        m_other->setEnabled(idle);
    if (m_sessionLink)
        m_sessionLink->setEnabled(idle);
    m_cancel->setVisible(
        state == State::Authenticating || state == State::Prompt || state == State::Cancelling);
    m_cancel->setEnabled(state == State::Authenticating || state == State::Prompt);
    showMessage(m_status); // Reposition the shared block after link visibility changes.
    if (idle && canSubmit)
        password()->setFocus();
}

void GreeterWindow::showUser()
{
    if (m_users.isEmpty())
        return;
    const auto &user = m_users.at(m_user);
    setUser(user.name.isEmpty() ? user.login : user.name, user.name.isEmpty() ? QString() : user.login);
}

void GreeterWindow::showSession()
{
    if (m_sessionLink && !m_sessions.isEmpty())
    {
        const auto name = m_sessions.at(m_session).name;
        m_sessionLink->setText(
            QFontMetrics(Karyo::Ui::bodyFont(14, QFont::DemiBold)).elidedText(name, Qt::ElideRight, 160));
        m_sessionLink->setToolTip(name);
    }
}

void GreeterWindow::chooseUser()
{
    if (m_auth->state() != GreeterAuthentication::State::Idle)
        return;
    QList<QPair<QString, QString>> choices;
    for (const auto &user : std::as_const(m_users))
        choices.append({ QStringLiteral("circle-user-round"), user.name.isEmpty() ? user.login : user.name });
    showChoices(m_other, choices,
        [this](int index)
        {
            if (m_auth->state() != GreeterAuthentication::State::Idle || index < 0 || index >= m_users.size())
                return;
            m_user = index;
            showUser();
            password()->clear();
            status(QString());
            password()->setFocus();
        });
}

void GreeterWindow::chooseSession()
{
    if (m_auth->state() != GreeterAuthentication::State::Idle)
        return;
    QList<QPair<QString, QString>> choices;
    for (const auto &session : std::as_const(m_sessions))
        choices.append({ QStringLiteral("monitor"), session.name });
    showChoices(m_sessionLink, choices,
        [this](int index)
        {
            if (m_auth->state() != GreeterAuthentication::State::Idle || index < 0
                || index >= m_sessions.size())
                return;
            m_session = index;
            showSession();
            password()->clear();
            password()->setFocus();
        });
}

void GreeterWindow::submit()
{
    using State = GreeterAuthentication::State;
    if (m_users.isEmpty() || m_sessions.isEmpty())
        return;
    const auto state = m_auth->state();
    if (state != State::Idle && state != State::Prompt)
        return;
    const QString response = password()->text();
    password()->clear();
    status(QString());
    if (state == State::Prompt)
        m_auth->answer(response);
    else
    {
        const auto &session = m_sessions.at(m_session);
        m_auth->begin(m_users.at(m_user).login, session.command, session.environment(), response);
    }
}
