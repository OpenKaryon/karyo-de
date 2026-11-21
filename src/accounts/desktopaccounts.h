// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QString>

namespace Karyo::Accounts
{

// The authoritative desktops.conf: main account first, its child desktops after it.
// Accounts not listed in the file are independent main accounts.
class DesktopAccounts
{
public:
    static DesktopAccounts load(const QString &path = { }, const QString &legacyPath = { });
    static DesktopAccounts parse(const QString &text);
    QString ownerOf(const QString &login) const;
    bool isChild(const QString &login) const { return ownerOf(login) != login; }
    bool valid() const { return m_valid; }

private:
    QHash<QString, QString> m_owners;
    bool m_valid = true;
};

} // namespace Karyo::Accounts
