// SPDX-License-Identifier: GPL-3.0-or-later
// The sign-in screen of KaryonLogin.dc.html, shared by the lock screen and
// the greeter: the mirrored wallpaper composition, the Karyo mark, a large
// clock and the date, the user with the password pill, links under it,
// the version line and a glass pill with language, network and power.
#pragma once

#include <QWidget>

#include <functional>

class QAbstractButton;
class QLabel;

namespace Karyo::Ui {
class PasswordField;
}

class SignInView : public QWidget
{
    Q_OBJECT
public:
    // decorationOnly: the backdrop alone, for screens other than the main one.
    explicit SignInView(bool decorationOnly = false, QWidget *parent = nullptr);

    void setUser(const QString &name, const QString &detail);
    Karyo::Ui::PasswordField *password() const { return m_password; }
    // Links under the password: "Other user", the session.
    QAbstractButton *addLink(const QString &text);
    void showMessage(const QString &text);
    // Shows a small card of choices above the anchor, inside this window:
    // lock surfaces and cage have no popups.
    void showChoices(QWidget *anchor, const QList<QPair<QString, QString>> &choices,
                     const std::function<void(int)> &chosen);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void tick();
    void placeUserBlock();
    void showPowerMenu();
    void closeChoices();

    bool m_decorationOnly;
    QString m_name;
    QString m_time;
    QString m_date;
    QString m_version;
    QString m_layout;
    QWidget *m_userBlock = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    Karyo::Ui::PasswordField *m_password = nullptr;
    QWidget *m_links = nullptr;
    QLabel *m_message = nullptr;
    QAbstractButton *m_power = nullptr;
    QWidget *m_choices = nullptr;
};
