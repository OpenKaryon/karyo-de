// SPDX-License-Identifier: GPL-3.0-or-later
// The session's polkit authentication agent: when an app asks for an
// administrator (App Center installing, Settings changing the time zone),
// the screen dims and a Karyo card asks for the password. It replaces
// lxqt-policykit-agent.
#pragma once

#include <polkitqt1-agent-listener.h>
#include <polkitqt1-identity.h>

#include <QPointer>

namespace PolkitQt1::Agent {
class Session;
}

class AuthDialog;

class PolkitAgent : public PolkitQt1::Agent::Listener
{
    Q_OBJECT
public:
    explicit PolkitAgent(QObject *parent = nullptr);
    // Registers for this login session; false if another agent has it.
    bool registerForSession();

public Q_SLOTS:
    void initiateAuthentication(const QString &actionId, const QString &message, const QString &iconName,
                                const PolkitQt1::Details &details, const QString &cookie,
                                const PolkitQt1::Identity::List &identities,
                                PolkitQt1::Agent::AsyncResult *result) override;
    bool initiateAuthenticationFinish() override;
    void cancelAuthentication() override;

private:
    void startSession();
    void finish();

    QPointer<AuthDialog> m_dialog;
    QPointer<PolkitQt1::Agent::Session> m_session;
    PolkitQt1::Agent::AsyncResult *m_result = nullptr;
    PolkitQt1::Identity::List m_identities;
    QString m_cookie;
    bool m_gained = false;
};
