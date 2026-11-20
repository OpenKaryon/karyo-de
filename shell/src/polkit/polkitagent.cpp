// SPDX-License-Identifier: GPL-3.0-or-later
#include "polkitagent.h"

#include "authdialog.h"

#include <polkitqt1-agent-session.h>
#include <polkitqt1-details.h>
#include <polkitqt1-subject.h>

#include <QGuiApplication>
#include <QScreen>

#include <pwd.h>
#include <unistd.h>

namespace {

// Include the account display name when it is available.
QString identityLabel(const PolkitQt1::Identity &identity)
{
    const PolkitQt1::UnixUserIdentity user(identity.toString().section(QLatin1Char(':'), 1));
    if (user.isValid()) {
        if (const passwd *pw = getpwuid(user.uid())) {
            const QString full = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
            const QString login = QString::fromLocal8Bit(pw->pw_name);
            return full.isEmpty() || full == login ? login : QStringLiteral("%1 (%2)").arg(full, login);
        }
    }
    return identity.toString();
}

} // namespace

PolkitAgent::PolkitAgent(QObject *parent)
    : PolkitQt1::Agent::Listener(parent)
{
}

bool PolkitAgent::registerForSession()
{
    return registerListener(PolkitQt1::UnixSessionSubject(getpid()),
                            QStringLiteral("/org/karyo/PolicyKit1/AuthenticationAgent"));
}

void PolkitAgent::initiateAuthentication(const QString &actionId, const QString &message, const QString &,
                                         const PolkitQt1::Details &details, const QString &cookie,
                                         const PolkitQt1::Identity::List &identities,
                                         PolkitQt1::Agent::AsyncResult *result)
{
    if (m_result) {
        // One at a time; polkit queues the rest.
        result->setError(QStringLiteral("Another authentication is in progress"));
        result->setCompleted();
        return;
    }
    m_result = result;
    m_cookie = cookie;
    m_gained = false;
    // The signed-in user first, when they may answer.
    m_identities = identities;
    const passwd *pw = getpwuid(getuid());
    const QString self = pw ? QStringLiteral("unix-user:") + QString::fromLocal8Bit(pw->pw_name) : QString();
    for (qsizetype i = 0; i < m_identities.size(); ++i) {
        if (m_identities.at(i).toString() == self) {
            m_identities.move(i, 0);
            break;
        }
    }
    QStringList labels;
    for (const PolkitQt1::Identity &identity : std::as_const(m_identities))
        labels.append(identityLabel(identity));

    // pkexec names the program; other actions show their id.
    QString detail = details.lookup(QStringLiteral("command_line"));
    if (detail.isEmpty())
        detail = details.lookup(QStringLiteral("program"));
    if (detail.isEmpty())
        detail = actionId;

    QScreen *screen = QGuiApplication::primaryScreen();
    m_dialog = new AuthDialog(message, detail, labels, screen);
    connect(m_dialog, &AuthDialog::submitted, this, [this] {
        if (!m_session)
            return;
        m_dialog->setBusy(true);
        m_session->setResponse(m_dialog->password());
    });
    connect(m_dialog, &AuthDialog::identityChanged, this, &PolkitAgent::startSession);
    connect(m_dialog, &AuthDialog::cancelled, this, [this] {
        if (m_session)
            m_session->cancel();
        finish();
    });
    m_dialog->show();
    startSession();
}

void PolkitAgent::startSession()
{
    if (m_session) {
        m_session->cancel();
        m_session->deleteLater();
    }
    const PolkitQt1::Identity identity = m_identities.value(m_dialog ? m_dialog->identityIndex() : 0);
    // No result here: polkit's request stays open across wrong passwords,
    // and finish() completes it once.
    m_session = new PolkitQt1::Agent::Session(identity, m_cookie, nullptr, this);
    connect(m_session, &PolkitQt1::Agent::Session::request, this, [this](const QString &request, bool) {
        if (m_dialog)
            m_dialog->askPassword(request);
    });
    connect(m_session, &PolkitQt1::Agent::Session::showError, this, [this](const QString &text) {
        if (m_dialog)
            m_dialog->showError(text);
    });
    connect(m_session, &PolkitQt1::Agent::Session::showInfo, this, [this](const QString &text) {
        if (m_dialog)
            m_dialog->showInfo(text);
    });
    connect(m_session, &PolkitQt1::Agent::Session::completed, this, [this](bool gained) {
        if (gained) {
            m_gained = true;
            finish();
            return;
        }
        // A wrong password: say so and ask again.
        if (m_dialog) {
            m_dialog->showError(tr("That password didn't work. Try again."));
            startSession();
        }
    });
    m_session->initiate();
}

void PolkitAgent::finish()
{
    if (m_session) {
        m_session->deleteLater();
        m_session = nullptr;
    }
    if (m_dialog)
        m_dialog->close();
    if (m_result) {
        m_result->setCompleted();
        m_result = nullptr;
    }
}

bool PolkitAgent::initiateAuthenticationFinish() { return m_gained; }

void PolkitAgent::cancelAuthentication()
{
    if (m_session)
        m_session->cancel();
    finish();
}
