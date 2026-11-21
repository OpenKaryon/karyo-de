// SPDX-License-Identifier: GPL-3.0-or-later
#include "archivepassworddialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ArchivePasswordDialog::ArchivePasswordDialog(const QString &archive, bool incorrect, QWidget *parent)
    : QDialog(parent), m_password(new QLineEdit(this))
{
    setObjectName(QStringLiteral("archivePasswordDialog"));
    setWindowTitle(tr("Archive password"));
    setWindowModality(Qt::WindowModal);
    resize(440, 220);
    auto *layout = new QVBoxLayout(this);
    auto *name = new QLabel(archive, this);
    name->setTextFormat(Qt::PlainText); name->setWordWrap(true); layout->addWidget(name);
    auto *hint = new QLabel(incorrect ? tr("Incorrect password. Please try again.") : tr("Enter the password to unlock this archive."), this);
    hint->setObjectName(QStringLiteral("archivePasswordHint")); hint->setWordWrap(true); layout->addWidget(hint);
    m_password->setObjectName(QStringLiteral("archivePassword"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("Password"));
    m_password->setAccessibleName(tr("Archive password"));
    layout->addWidget(m_password);
    auto *show = new QCheckBox(tr("Show password"), this); layout->addWidget(show);
    connect(show, &QCheckBox::toggled, this, [this](bool visible) {
        m_password->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Unlock"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_password, &QLineEdit::textChanged, this, [buttons](const QString &text) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    m_password->setFocus();
}
QString ArchivePasswordDialog::password() const { return m_password->text(); }
