// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-editor [FILE...]: Karyo Text Editor. One window for everything:
// a second start hands its files to the running editor over D-Bus.
#include "editorwindow.h"

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
    QApplication::setApplicationName(QStringLiteral("karyo-editor"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    // Public Wayland identity; Karyo supplies client-side decoration rules.
    QApplication::setDesktopFileName(QStringLiteral("karyo-editor"));
    Karyo::Ui::applyStyle();

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-editor"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    QStringList files;
    for (const QString &arg : QApplication::arguments().mid(1)) {
        if (!arg.startsWith(QLatin1Char('-')))
            files.append(QDir::current().absoluteFilePath(arg));
    }

    // Keep the existing wire endpoint: prototype and public launches must not
    // create competing windows that write the same recovery cache.
    const QString service = QStringLiteral("org.openkaryon.Editor");
    const QString path = QStringLiteral("/org/openkaryon/Editor");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && !bus.registerService(service)) {
        QDBusInterface running(service, path, service, bus);
        const QDBusReply<void> reply = running.call(QStringLiteral("Open"), files);
        if (reply.isValid())
            return 0;
    }

    EditorWindow window;
    if (bus.isConnected())
        bus.registerObject(path, new EditorService(&window), QDBusConnection::ExportScriptableSlots);
    const bool restored = window.restoreSession();
    if (!files.isEmpty())
        window.openFiles(files);
    else if (!restored)
        window.newUntitled();
    window.show();
    return QApplication::exec();
}
