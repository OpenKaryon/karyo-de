// SPDX-License-Identifier: GPL-3.0-or-later
// Checks a password with PAM in a worker thread, so the clock keeps
// running while pam_unix waits out its delay after a wrong password.
#pragma once

#include <QObject>

class PamCheck : public QObject
{
    Q_OBJECT
public:
    explicit PamCheck(const QString &service, QObject *parent = nullptr);

    void check(const QString &user, const QString &password);

Q_SIGNALS:
    void finished(bool ok);

private:
    QByteArray m_service;
};
