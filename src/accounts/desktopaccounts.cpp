// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktopaccounts.h"
#include "profileconfig.h"
#include <QStringList>
namespace Karyo::Accounts
{
DesktopAccounts DesktopAccounts::load(const QString &path, const QString &legacyPath)
{
    const QString current = path.isEmpty() ? QStringLiteral("/etc/karyo/desktops.conf") : path;
    const QString legacy = path.isEmpty() && legacyPath.isEmpty()
        ? QStringLiteral("/etc/openkaryon/desktops.conf")
        : legacyPath;
    const auto input = Internal::readConfig(current, legacy);
    if (input.state == Internal::ReadState::Missing)
        return { };
    if (input.state == Internal::ReadState::Found)
        return parse(input.text);
    DesktopAccounts invalid;
    invalid.m_valid = false;
    return invalid;
}
DesktopAccounts DesktopAccounts::parse(const QString &text)
{
    DesktopAccounts accounts;
    if (!Internal::boundedText(text))
    {
        accounts.m_valid = false;
        return accounts;
    }
    for (const QString &line : text.split(Internal::lineBreaks()))
    {
        const auto names
            = line.section(QLatin1Char('#'), 0, 0).split(Internal::whitespace(), Qt::SkipEmptyParts);
        for (const auto &login : names)
        {
            if (!Internal::validLogin(login) || accounts.m_owners.contains(login))
            {
                accounts.m_owners.clear();
                accounts.m_valid = false;
                return accounts;
            }
            accounts.m_owners.insert(login, names.first());
        }
    }
    return accounts;
}
QString DesktopAccounts::ownerOf(const QString &login) const { return m_owners.value(login, login); }
} // namespace Karyo::Accounts
