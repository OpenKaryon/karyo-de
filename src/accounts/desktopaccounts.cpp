// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktopaccounts.h"
#include <QFileInfo>

#include <QFile>
#include <QRegularExpression>
#include <QStringList>

namespace Karyo::Accounts {

DesktopAccounts DesktopAccounts::load(const QString &path)
{
    const QString current = QStringLiteral("/etc/karyo/desktops.conf");
    const QString selected = path.isEmpty()
        ? (QFileInfo::exists(current) ? current : QStringLiteral("/etc/openkaryon/desktops.conf")) : path;
    QFile file(selected);
    if (!file.exists())
        return {};
    if (file.size() <= 1024 * 1024 && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray data = file.readAll();
        if (file.error() == QFileDevice::NoError)
            return parse(QString::fromUtf8(data));
    }
    DesktopAccounts accounts;
    accounts.m_valid = false;
    return accounts;
}

DesktopAccounts DesktopAccounts::parse(const QString &text)
{
    DesktopAccounts accounts;
    const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QRegularExpression name(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]*\\$?$"));
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QStringList names = line.section(QLatin1Char('#'), 0, 0).trimmed().split(whitespace, Qt::SkipEmptyParts);
        for (const QString &login : names) {
            if (!name.match(login).hasMatch() || accounts.m_owners.contains(login)) {
                accounts.m_owners.clear();
                accounts.m_valid = false;
                return accounts;
            }
            accounts.m_owners.insert(login, names.first());
        }
    }
    return accounts;
}

QString DesktopAccounts::ownerOf(const QString &login) const
{
    return m_owners.value(login, login);
}

} // namespace Karyo::Accounts
