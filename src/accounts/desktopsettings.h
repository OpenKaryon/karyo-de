// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace Karyo::Accounts
{
class DesktopSettings
{
public:
    static DesktopSettings parse(const QString &text);
    static DesktopSettings load(const QString &login,
        const QString &configuration = QStringLiteral("/etc/karyo"),
        const QString &legacy = QStringLiteral("/etc/openkaryon"));
    bool valid() const { return m_valid; }
    bool protectedEntry() const { return !m_valid || m_password != QLatin1String("none"); }
    QString passwordMode() const { return m_password; }
    QString vpn() const { return m_vpn; }
    bool workstation() const { return m_workstation; }

private:
    bool m_valid = true;
    QString m_password = QStringLiteral("none");
    QString m_vpn = QStringLiteral("none");
    bool m_workstation = false;
};
} // namespace Karyo::Accounts
