// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-settings: Settings (design 1h). "karyo-settings SECTION"
// opens a section: system, devices, network, personalization, apps,
// accounts, time, privacy, updates, about, or storage (System › Storage).
#include "settingswindow.h"

#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-settings"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    QApplication::setDesktopFileName(QStringLiteral("karyo-settings"));
    Karyo::Ui::applyStyle();
    // The theme and accent chosen here show at once, here and in the shell.
    Karyo::Ui::Theme::followConfiguration();

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-settings"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    SettingsWindow window;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.showSection(args.at(1));
    window.resizeToFit({1440, 900});
    window.show();
    return QApplication::exec();
}
