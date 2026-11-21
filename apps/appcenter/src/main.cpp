// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcenterwindow.h"
#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-appcenter"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    QApplication::setDesktopFileName(QStringLiteral("karyo-appcenter"));
    Karyo::Ui::applyStyle();
    Karyo::Ui::Theme::followConfiguration();
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(
            QLocale(), QStringLiteral("karyo-appcenter"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);
    AppCenterWindow window;
    const auto args = QApplication::arguments();
    if (args.size() > 2 && args.at(1) == QLatin1String("--app"))
        window.showApp(args.at(2));
    else if (args.size() > 1)
        window.showSection(args.at(1));
    window.resizeToFit({ 1520, 900 });
    window.show();
    return QApplication::exec();
}
