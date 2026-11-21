// SPDX-License-Identifier: GPL-3.0-or-later
#include "authentication.h"
#include <algorithm>

GreeterAuthentication::GreeterAuthentication(Greetd *transport, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(transport);
    connect(transport, &Greetd::response, this, &GreeterAuthentication::handle);
    connect(transport, &Greetd::failed, this, &GreeterAuthentication::fatal);
    connect(transport, &Greetd::disconnected, this,
        [this]
        {
            if (m_state != State::Finished && m_state != State::Failed)
                fatal(tr("The sign-in service connection was lost."));
        });
    connect(transport, &QObject::destroyed, this,
        [this]
        {
            if (m_state != State::Finished && m_state != State::Failed)
                fatal(tr("The sign-in service is no longer available."));
        });
}

void GreeterAuthentication::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

void GreeterAuthentication::eraseFirstAnswer()
{
    m_firstAnswer.fill(QChar::Null);
    m_firstAnswer.clear();
    m_hasFirstAnswer = false;
}

bool GreeterAuthentication::begin(const QString &user, const QStringList &command,
    const QStringList &environment, const QString &firstAnswer)
{
    if (m_state != State::Idle)
        return false;
    if (!m_transport || !m_transport->isConnected())
    {
        fatal(tr("The sign-in service is not connected."));
        return false;
    }
    const auto containsNull = [](const QString &text) { return text.contains(QChar::Null); };
    if (containsNull(firstAnswer))
    {
        Q_EMIT message(tr("The response contains an unsupported character."), false);
        return false;
    }
    if (user.isEmpty() || containsNull(user) || command.isEmpty() || command.first().isEmpty()
        || std::any_of(command.begin(), command.end(), containsNull)
        || std::any_of(environment.begin(), environment.end(), containsNull))
    {
        Q_EMIT message(tr("Choose a valid user and session."), false);
        return false;
    }
    m_user = user;
    m_command = command;
    m_environment = environment;
    m_firstAnswer = firstAnswer;
    m_hasFirstAnswer = true;
    m_cancelRequested = false;
    m_deniedCleanupRetry = false;
    setState(State::Authenticating);
    m_transport->createSession(m_user);
    return m_state != State::Failed;
}

bool GreeterAuthentication::answer(const QString &response)
{
    if (m_state != State::Prompt || !m_transport)
        return false;
    if (response.contains(QChar::Null))
    {
        Q_EMIT message(tr("The response contains an unsupported character."), false);
        return false;
    }
    setState(State::Authenticating);
    m_transport->answer(response);
    return m_state != State::Failed;
}

bool GreeterAuthentication::cancel()
{
    if (m_state != State::Authenticating && m_state != State::Prompt)
        return false;
    eraseFirstAnswer();
    if (m_state == State::Prompt)
        cancelNow();
    else
        // Finish the outstanding request before issuing another IPC command.
        // Its next success must cancel, never start the chosen session.
        m_cancelRequested = true;
    return true;
}

void GreeterAuthentication::cancelNow()
{
    if (!m_transport)
    {
        fatal(tr("The sign-in service is no longer available."));
        return;
    }
    eraseFirstAnswer();
    m_cancelRequested = false;
    setState(State::Cancelling);
    m_transport->cancelSession();
}

void GreeterAuthentication::reset()
{
    eraseFirstAnswer();
    m_user.clear();
    m_command.clear();
    m_environment.clear();
    m_cancelRequested = false;
    m_deniedCleanupRetry = false;
    setState(State::Idle);
}

void GreeterAuthentication::fatal(const QString &text)
{
    if (m_state == State::Failed || m_state == State::Finished)
        return;
    eraseFirstAnswer();
    m_user.clear();
    m_command.clear();
    m_environment.clear();
    m_cancelRequested = false;
    m_deniedCleanupRetry = false;
    setState(State::Failed);
    Q_EMIT message(text, false);
}

void GreeterAuthentication::handle(const QJsonObject &reply)
{
    if (m_state == State::Failed || m_state == State::Finished)
        return;
    if (m_state == State::Idle || m_state == State::Prompt)
    {
        fatal(tr("The sign-in service replied out of sequence."));
        return;
    }
    const QString type = reply.value(QStringLiteral("type")).toString();
    if (m_cancelRequested)
    {
        m_deniedCleanupRetry = type == QLatin1String("error")
            && reply.value(QStringLiteral("error_type")).toString() == QLatin1String("auth_error");
        cancelNow();
        return;
    }
    if (type == QLatin1String("success"))
    {
        if (m_state == State::Authenticating)
        {
            eraseFirstAnswer();
            setState(State::Starting);
            m_transport->startSession(m_command, m_environment);
        }
        else if (m_state == State::Starting)
        {
            eraseFirstAnswer();
            setState(State::Finished);
            Q_EMIT sessionReady(m_user);
        }
        else if (m_state == State::Cancelling)
            reset();
        else
            fatal(tr("The sign-in service replied out of sequence."));
        return;
    }
    if (type == QLatin1String("error"))
    {
        if (m_state == State::Cancelling)
        {
            if (m_deniedCleanupRetry && m_transport)
            {
                // greetd 0.10 removes the configuring session before asking its
                // worker to cancel. A denied PAM worker can already be gone,
                // making that cleanup report an error. Retry once and require
                // a positive cancellation acknowledgement before allowing reuse.
                m_deniedCleanupRetry = false;
                m_transport->cancelSession();
                return;
            }
            fatal(tr("Could not cancel the sign-in attempt."));
            return;
        }
        const bool denied
            = reply.value(QStringLiteral("error_type")).toString() == QLatin1String("auth_error");
        m_deniedCleanupRetry = denied;
        Q_EMIT message(denied ? tr("That password didn't work. Try again.")
                              : reply.value(QStringLiteral("description")).toString(),
            denied);
        cancelNow();
        return;
    }
    if (type != QLatin1String("auth_message") || m_state != State::Authenticating)
    {
        fatal(tr("The sign-in service replied out of sequence."));
        return;
    }
    const QString kind = reply.value(QStringLiteral("auth_message_type")).toString();
    const QString text = reply.value(QStringLiteral("auth_message")).toString().trimmed();
    if (kind == QLatin1String("info") || kind == QLatin1String("error"))
    {
        Q_EMIT message(text, kind == QLatin1String("error"));
        m_transport->acknowledge();
        return;
    }
    const bool secret = kind == QLatin1String("secret");
    if (!secret && kind != QLatin1String("visible"))
    {
        fatal(tr("The sign-in service sent an unsupported question."));
        return;
    }
    if (secret && m_hasFirstAnswer)
    {
        const QString answer = m_firstAnswer;
        eraseFirstAnswer();
        m_transport->answer(answer);
        return;
    }
    // Do not send a pretyped password to a visible username/code question.
    eraseFirstAnswer();
    setState(State::Prompt);
    Q_EMIT promptRequested(text, secret);
}
