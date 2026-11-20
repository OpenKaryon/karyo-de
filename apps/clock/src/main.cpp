// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-clock [--stopwatch]: Karyo Clock, with named timers for the
// bench and a stopwatch. One instance: a second start shows the running
// one over D-Bus, which may be hidden in the tray with timers going.
#include "clockwindow.h"

#include <karyo/ui/style.h>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-clock"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    // Public Wayland identity; Karyo supplies client-side decoration rules.
    QApplication::setDesktopFileName(QStringLiteral("karyo-clock"));
    // Closing the window with timers running keeps Clock in the tray.
    QApplication::setQuitOnLastWindowClosed(false);
    Karyo::Ui::applyStyle();

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-clock"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    const QString mode = QApplication::arguments().contains(QStringLiteral("--stopwatch")) ? QStringLiteral("stopwatch")
                                                                                            : QString();
    const QString service = QStringLiteral("org.openkaryon.Clock");
    const QString path = QStringLiteral("/org/openkaryon/Clock");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && !bus.registerService(service)) {
        QDBusInterface running(service, path, service, bus);
        const QDBusReply<void> reply = running.call(QStringLiteral("Show"), mode);
        if (reply.isValid())
            return 0;
    }

    ClockWindow window;
    if (bus.isConnected())
        bus.registerObject(path, new ClockService(&window), QDBusConnection::ExportScriptableSlots);
    if (!mode.isEmpty())
        window.showStopwatch();
    window.show();
    return QApplication::exec();
}
