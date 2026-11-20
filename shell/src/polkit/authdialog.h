// SPDX-License-Identifier: GPL-3.0-or-later
// The authentication card of the polkit agent: a dimmed full-screen overlay
// that holds the keyboard, and a centered card with what asks, why, for
// whom, the password and Cancel.
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QScreen;

namespace Karyo::Ui {
class PasswordField;
}

class AuthDialog : public QWidget
{
    Q_OBJECT
public:
    AuthDialog(const QString &message, const QString &detail, const QStringList &identities, QScreen *screen);

    int identityIndex() const;
    QString password() const;
    // The PAM prompt ("Password: ") is ready for an answer.
    void askPassword(const QString &prompt);
    void showError(const QString &text);
    void showInfo(const QString &text);
    void setBusy(bool busy);

Q_SIGNALS:
    void submitted();
    void identityChanged();
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QWidget *m_card;
    QComboBox *m_identities;
    Karyo::Ui::PasswordField *m_password;
    QLabel *m_status;
};
