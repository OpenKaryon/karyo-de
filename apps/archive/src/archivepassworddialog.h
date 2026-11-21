// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>
class QLineEdit;
class ArchivePasswordDialog : public QDialog
{
    Q_OBJECT
public:
    ArchivePasswordDialog(const QString &archive, bool incorrect, QWidget *parent = nullptr);
    QString password() const;
private:
    QLineEdit *m_password;
};
