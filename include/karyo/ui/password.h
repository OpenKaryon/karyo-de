// SPDX-License-Identifier: GPL-3.0-or-later
// The sign-in password pill (KaryonLogin.dc.html): a lock glyph and the
// dots in a 54px pill with an accent border while focused, then the round
// accent button with an arrow that submits. The polkit agent, the lock
// screen and the greeter use it.
#pragma once

#include <QWidget>

class QAbstractButton;
class QLineEdit;
class QVariantAnimation;

namespace Karyo::Ui {

class PasswordField : public QWidget
{
    Q_OBJECT
public:
    explicit PasswordField(QWidget *parent = nullptr);

    QLineEdit *lineEdit() const { return m_edit; }
    QString text() const;
    void clear();
    void setPlaceholder(const QString &text);
    // While checking: the field and the button are disabled.
    void setBusy(bool busy);
    // A wrong password: the field shakes and the border turns to the error ink.
    void showError();
    QSize sizeHint() const override;

Q_SIGNALS:
    void submitted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QRect pillRect() const;

    QLineEdit *m_edit;
    QAbstractButton *m_go;
    QVariantAnimation *m_shake;
    int m_offset = 0;
    bool m_error = false;
    bool m_busy = false;
};

} // namespace Karyo::Ui
