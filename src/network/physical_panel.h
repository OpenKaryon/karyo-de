// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace Karyo::Ui {
class PhysicalNetworkPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PhysicalNetworkPanel(QWidget *parent = nullptr);
    void refresh();
Q_SIGNALS:
    void geometryChanged();
protected:
    void showEvent(QShowEvent *event) override;
private:
    QJsonObject selected() const;
    void updateAction();
    void activate();
    void change(QJsonObject request);
    void status(const QString &message);
    QListWidget *m_list;
    QPushButton *m_radio;
    QPushButton *m_action;
    QLabel *m_status;
    QLabel *m_hint;
    QLineEdit *m_password;
    QWidget *m_form;
    bool m_loading = false;
    bool m_busy = false;
    bool m_wirelessEnabled = false;
    QString m_owner;
};
}
