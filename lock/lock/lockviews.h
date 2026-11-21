// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class QScreen;
class SignInView;
class PamCheck;
namespace SessionLock
{
class Lock;
}

// Every actual output has an interactive form. One shared PAM request survives
// screen replacement, while removed screens never remain the only input view.
class LockViews : public QObject
{
public:
    LockViews(SessionLock::Lock *lock, QString login, QString fullName, std::function<void()> authenticated);
    ~LockViews() override;

private:
    void scheduleReconcile();
    void reconcile();
    void removeScreen(QScreen *screen);
    void submit(SignInView *view);
    void checked(bool accepted);
    SessionLock::Lock *m_lock;
    PamCheck *m_check;
    QHash<QScreen *, SignInView *> m_views;
    QPointer<SignInView> m_submitted;
    QString m_login;
    QString m_fullName;
    QString m_message;
    std::function<void()> m_authenticated;
    bool m_checking = false;
    bool m_scheduled = false;
};
