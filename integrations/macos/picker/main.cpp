// SPDX-License-Identifier: GPL-3.0-or-later
#include "pickerwindow.h"
#include <karyo/ui/style.h>
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char **argv)
{
    // The arguments belong to the launched application, including tokens such
    // as -platform/-style. Do not let QApplication consume them before our
    // explicit positional parser sees them.
    QStringList originalArguments;
    for (int index = 0; index < argc; ++index)
        originalArguments.append(QString::fromLocal8Bit(argv[index]));
    int qtArgc = 1;
    char *qtArgv[] = {argv[0], nullptr};
    QApplication app(qtArgc, qtArgv);
    app.setApplicationName(QStringLiteral("karyo-macos"));
    app.setOrganizationName(QStringLiteral("Karyo"));
    app.setDesktopFileName(QStringLiteral("karyo-macos"));
    Karyo::Ui::applyStyle();
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("karyo-macos")));
    QTranslator qtTranslation;
    if (qtTranslation.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) app.installTranslator(&qtTranslation);
    QTranslator translation;
    if (translation.load(QLocale(), QStringLiteral("karyo-macos-picker"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        app.installTranslator(&translation);
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("MacOSPicker", "Experimental Intel macOS application launcher"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("application"), QStringLiteral("Optional .app folder or Intel Mach-O executable."));
    parser.addPositionalArgument(QStringLiteral("arguments"), QStringLiteral("Arguments forwarded literally to the application."), QStringLiteral("[arguments…]"));
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);
    parser.process(originalArguments);
    QStringList positional = parser.positionalArguments();
    const QString initial = positional.isEmpty() ? QString() : positional.takeFirst();
    MacOSPicker window(QStringLiteral(KARYO_MACOS_HELPER_PATH), initial, positional);
    window.show();
    return app.exec();
}
