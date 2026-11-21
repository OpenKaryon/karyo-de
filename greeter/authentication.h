// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "greetd.h"
#include <QPointer>
#include <QStringList>

// Authentication/session protocol only. The daemon owns PAM and authorization;
// the selected command/environment are immutable for one authentication attempt.
class GreeterAuthentication : public QObject
{
    Q_OBJECT
public:
    enum class State
    {
        Idle,
        Authenticating,
        Prompt,
        Starting,
        Cancelling,
        Finished,
        Failed
    };
    Q_ENUM(State)
    explicit GreeterAuthentication(Greetd *transport, QObject *parent = nullptr);
    State state() const { return m_state; }
    bool begin(const QString &user, const QStringList &command, const QStringList &environment,
        const QString &firstAnswer);
    bool answer(const QString &response);
    bool cancel();
Q_SIGNALS:
    void stateChanged(GreeterAuthentication::State state);
    void promptRequested(const QString &text, bool secret);
    void message(const QString &text, bool authenticationError);
    void sessionReady(const QString &user);

private:
    void handle(const QJsonObject &reply);
    void setState(State state);
    void cancelNow();
    void reset();
    void eraseFirstAnswer();
    void fatal(const QString &message);
    QPointer<Greetd> m_transport;
    State m_state = State::Idle;
    QString m_user, m_firstAnswer;
    QStringList m_command, m_environment;
    bool m_hasFirstAnswer = false, m_cancelRequested = false;
};
