// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktopaccounts.h"
#include <QTemporaryDir>
#include <QFileInfo>

#include <QCoreApplication>
#include <QDebug>
#include <QTemporaryFile>

using Karyo::Accounts::DesktopAccounts;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto check = [](bool ok, const char *message) {
        if (!ok)
            qFatal("%s", message);
    };
    const auto accounts = DesktopAccounts::parse(QStringLiteral(
        "# Main followed by project desktops\nowner work test # comment\nanother lab\n"));
    check(accounts.valid(), "valid existing config");
    check(!accounts.isChild(QStringLiteral("owner")), "main visible at sign-in");
    check(!accounts.isChild(QStringLiteral("another")), "other main visible at sign-in");
    check(accounts.isChild(QStringLiteral("work")), "child hidden at sign-in");
    check(accounts.ownerOf(QStringLiteral("test")) == QStringLiteral("owner"), "child owner");
    check(accounts.ownerOf(QStringLiteral("lab")) == QStringLiteral("another"), "families separate");
    check(!accounts.isChild(QStringLiteral("new-user")), "unlisted accounts are main");
    check(!DesktopAccounts::parse(QStringLiteral("owner work\nwork nested\n")).valid(), "no third level");
    check(!DesktopAccounts::parse(QStringLiteral("owner work\nanother work\n")).valid(), "one owner per child");
    check(!DesktopAccounts::parse(QStringLiteral("owner owner\n")).valid(), "no duplicates");
    check(!DesktopAccounts::parse(QStringLiteral("owner --invalid\n")).valid(), "invalid account");
    QTemporaryFile file;
    check(file.open(), "temporary config");
    file.write("owner work\n");
    file.flush();
    check(DesktopAccounts::load(file.fileName()).isChild(QStringLiteral("work")), "load config");
    check(!DesktopAccounts::load(QStringLiteral("/nonexistent/karyo-desktops-test")).isChild(QStringLiteral("owner")),
          "missing config preserves standalone accounts");
    QTemporaryDir directory;
    check(!DesktopAccounts::load(directory.path()).valid(), "unreadable configuration is reported invalid");
    check(!DesktopAccounts::parse(QStringLiteral("owner work\ninvalid;name child\n")).valid(), "invalid syntax is rejected");
    check(!DesktopAccounts::parse(QStringLiteral("owner work\nother work\n")).isChild("work"), "invalid maps expose no trusted ownership");
    qInfo("PASS: main/child account discovery");
}
