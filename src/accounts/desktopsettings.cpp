// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktopsettings.h"
#include "profileconfig.h"
#include <QHash>
#include <QSet>
#include <QStringList>
namespace Karyo::Accounts
{
DesktopSettings DesktopSettings::load(
    const QString &login, const QString &configuration, const QString &legacy)
{
    DesktopSettings result;
    if (!Internal::validLogin(login))
    {
        result.m_valid = false;
        return result;
    }
    const QString relative = QStringLiteral("/desktops.d/") + login + QStringLiteral(".conf");
    const auto input
        = Internal::readConfig(configuration + relative, legacy.isEmpty() ? QString() : legacy + relative);
    if (input.state == Internal::ReadState::Found)
        return parse(input.text);
    result.m_valid = input.state == Internal::ReadState::Missing;
    return result;
}
DesktopSettings DesktopSettings::parse(const QString &text)
{
    DesktopSettings result;
    if (!Internal::boundedText(text))
    {
        result.m_valid = false;
        return result;
    }
    const QHash<QString, QSet<QString>> allowed {
        { QStringLiteral("PASSWORD"),
            { QStringLiteral("none"), QStringLiteral("home"), QStringLiteral("separate") } },
        { QStringLiteral("VPN"), { QStringLiteral("none"), QStringLiteral("netbird") } },
        { QStringLiteral("WORKSTATION"), { QStringLiteral("yes"), QStringLiteral("no") } }
    };
    QHash<QString, QString> values;
    for (const auto &line : text.split(Internal::lineBreaks()))
    {
        const auto literal = Internal::stripped(line.section(QLatin1Char('#'), 0, 0));
        if (literal.isEmpty())
            continue;
        const auto separator = literal.indexOf(QLatin1Char('='));
        const auto key = Internal::stripped(literal.left(separator));
        auto value = Internal::stripped(literal.mid(separator + 1));
        if (value.size() >= 2 && value.front() == value.back()
            && (value.front() == QLatin1Char('"') || value.front() == QLatin1Char('\'')))
            value = value.mid(1, value.size() - 2);
        if (separator < 0 || values.contains(key) || !allowed.value(key).contains(value))
        {
            result.m_valid = false;
            return result;
        }
        values.insert(key, value);
    }
    result.m_password = values.value(QStringLiteral("PASSWORD"), QStringLiteral("none"));
    result.m_vpn = values.value(QStringLiteral("VPN"), QStringLiteral("none"));
    result.m_workstation = values.value(QStringLiteral("WORKSTATION")) == QLatin1String("yes");
    return result;
}
} // namespace Karyo::Accounts
