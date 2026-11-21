// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktopsettings.h"
#include "signin.h"
#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLockFile>
#include <QStandardPaths>
#include <QTranslator>
#include <cstdio>
#include <cstring>
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>

int main(int argc, char **argv)
{
    const bool startup = argc == 2 && std::strcmp(argv[1], "--on-start") == 0;
    const bool entryCheck = argc == 2 && std::strcmp(argv[1], "--entry-check") == 0;
    if (argc == 2 && std::strcmp(argv[1], "--help") == 0)
    {
        std::puts("Usage: karyo-lock [--on-start|--entry-check]\n"
                  "--entry-check: 0 unprotected, 3 protected, 1 invalid system policy");
        return 0;
    }
    if (argc != 1 && !startup && !entryCheck)
    {
        std::fputs("Usage: karyo-lock [--on-start|--entry-check]\n", stderr);
        return 2;
    }
    // Run before QApplication: unprotected startup needs no display connection,
    // and malformed policy must never turn into an ordinary desktop launch.
    if (startup || entryCheck)
    {
        const auto policy = Karyo::Accounts::DesktopSettings::current();
        if (!policy.valid())
        {
            std::fputs("karyo-lock: invalid system desktop protection policy\n", stderr);
            return 1;
        }
        if (entryCheck)
            return policy.protectedEntry() ? 3 : 0;
        if (!policy.protectedEntry())
            return 0;
    }
    qputenv("QT_QPA_PLATFORM", "wayland");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("karyo-lock"));
    app.setOrganizationName(QStringLiteral("karyo"));
    app.setDesktopFileName(QStringLiteral("karyo-lock"));
    QLockFile instance(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + QStringLiteral("/karyo-lock.lock"));
    if (!instance.tryLock(0))
        return 1;
    Karyo::Ui::applyStyle();
    Karyo::Ui::Theme::followConfiguration();
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(
            QLocale(), QStringLiteral("karyo-lock"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);
    return runLock();
}
