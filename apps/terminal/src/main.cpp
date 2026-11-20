// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-terminal [-w DIR] [-e COMMAND [ARGS...]]
#include "terminalwindow.h"

#include <karyo/ui/style.h>

#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-terminal"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    // Public Wayland app_id; the Karyo session uses client-side decorations.
    QApplication::setDesktopFileName(QStringLiteral("karyo-terminal"));
    Karyo::Ui::applyStyle();

    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-terminal"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    // x-terminal-emulator conventions: -w/--workdir, -e/-x and everything after it.
    QString directory = QDir::currentPath();
    QString program;
    QStringList args;
    const QStringList arguments = QApplication::arguments().mid(1);
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString &arg = arguments.at(i);
        if ((arg == QLatin1String("-w") || arg == QLatin1String("--workdir")) && i + 1 < arguments.size()) {
            directory = arguments.at(++i);
        } else if (arg == QLatin1String("-e") || arg == QLatin1String("-x")
                   || arg == QLatin1String("--execute")) {
            const QStringList rest = arguments.mid(i + 1);
            if (rest.size() == 1 && rest.first().contains(QLatin1Char(' '))) {
                program = QStringLiteral("/bin/sh");
                args = {QStringLiteral("-c"), rest.first()};
            } else if (!rest.isEmpty()) {
                program = rest.first();
                args = rest.mid(1);
            }
            break;
        }
    }
    if (directory == QLatin1String("/"))
        directory = QDir::homePath();

    TerminalWindow window;
    window.newTab(directory, program, args);
    window.show();
    return QApplication::exec();
}
