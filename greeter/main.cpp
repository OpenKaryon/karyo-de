// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"
#include "greeterwindow.h"
#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>
#include <cstdio>
#include <karyo/ui/password.h>
#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>

int main(int argc, char **argv)
{
    if (argc != 1)
    {
        const bool help = argc == 2 && qstrcmp(argv[1], "--help") == 0;
        std::fputs("Usage: karyo-greeter\nRun as a greetd greeter under a Wayland kiosk compositor.\n",
            help ? stdout : stderr);
        return help ? 0 : 2;
    }
    qputenv("QT_QPA_PLATFORM", "wayland");
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-greeter"));
    QApplication::setOrganizationName(QStringLiteral("karyo"));
    QApplication::setDesktopFileName(QStringLiteral("karyo-greeter"));
    QTranslator qtTranslator, translator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    if (translator.load(
            QLocale(), QStringLiteral("karyo-greeter"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);
    const auto accounts = Karyo::Accounts::DesktopAccounts::load();
    QString inventoryError;
    const auto users = systemGreeterUsers(accounts, &inventoryError);
    const auto sessions = greeterSessions();
    auto selected = readGreeterSelection();
    selected.user = accounts.ownerOf(selected.user);
    int userIndex = 0;
    for (int i = 0; i < users.size(); ++i)
        if (users.at(i).login == selected.user)
            userIndex = i;
    const auto appearance = users.isEmpty()
        ? GreeterAppearance()
        : readGreeterAppearance(users.at(userIndex).login, users.at(userIndex).uid);
    if (!applyGreeterAppearance(appearance))
        std::fputs("karyo-greeter: could not save appearance preferences\n", stderr);
    Karyo::Ui::Theme::followConfiguration();
    Karyo::Ui::applyStyle();
    Greetd transport;
    if (!transport.connectToDaemon())
    {
        std::fputs("karyo-greeter: cannot connect to greetd\n", stderr);
        return 1;
    }
    if (!inventoryError.isEmpty())
        selected.error = inventoryError;
    GreeterWindow window(&transport, users, sessions, selected);
    QObject::connect(&window, &GreeterWindow::accepted, &app, &QCoreApplication::quit);
    QObject::connect(&window, &GreeterWindow::preferenceError, &app,
        [](const QString &text) { std::fprintf(stderr, "karyo-greeter: %s\n", qPrintable(text)); });
    QObject::connect(&window, &GreeterWindow::connectionFailed, &app,
        [&app] { QTimer::singleShot(1000, &app, [&app] { app.exit(1); }); });
    window.showFullScreen();
    window.password()->setFocus();
    return QApplication::exec();
}
