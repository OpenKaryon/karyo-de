// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "authentication.h"
#include "inventory.h"
#include "signinview.h"
class QAbstractButton;
class GreeterWindow : public SignInView
{
    Q_OBJECT
public:
    GreeterWindow(Greetd *transport, QList<GreeterUser> users, QList<GreeterSession> sessions,
        const GreeterSelection &selected, const QString &preferencesRoot = { }, QWidget *parent = nullptr);
Q_SIGNALS:
    void accepted(const QString &user, const QString &session);
    void preferenceError(const QString &message);
    void connectionFailed();

private:
    void submit();
    void sync();
    void showUser();
    void showSession();
    void chooseUser();
    void chooseSession();
    void status(const QString &message);
    GreeterAuthentication *m_auth;
    QList<GreeterUser> m_users;
    QList<GreeterSession> m_sessions;
    int m_user = 0, m_session = 0;
    QAbstractButton *m_other = nullptr, *m_sessionLink = nullptr, *m_cancel = nullptr;
    QString m_preferencesRoot, m_status;
    bool m_cancelled = false;
};
