// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-reader [FILE...]: Karyo PDF Reader. One window for every
// document: a second start hands its files to the running reader over D-Bus.
#include "readerwindow.h"

#include <karyo/ui/style.h>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-reader"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    // Public Wayland identity; Karyo supplies client-side decorations.
    QApplication::setDesktopFileName(QStringLiteral("karyo-reader"));
    Karyo::Ui::applyStyle();

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-reader"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    QStringList files;
    for (const QString &arg : QApplication::arguments().mid(1)) {
        if (!arg.startsWith(QLatin1Char('-')))
            files.append(QDir::current().absoluteFilePath(arg));
    }

    const QString service = QStringLiteral("org.openkaryon.Reader");
    const QString path = QStringLiteral("/org/openkaryon/Reader");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && !bus.registerService(service)) {
        QDBusInterface running(service, path, service, bus);
        const QDBusReply<void> reply = running.call(QStringLiteral("Open"), files);
        if (reply.isValid())
            return 0;
    }

    ReaderWindow window;
    if (bus.isConnected())
        bus.registerObject(path, new ReaderService(&window), QDBusConnection::ExportScriptableSlots);
    window.openFiles(files);
    window.show();
    return QApplication::exec();
}
