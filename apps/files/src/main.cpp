// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-files [--select] [--new-window] [FOLDER|FILE|URL...]
#include "filesservice.h"

#include <karyo/ui/style.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QDir>
#include <QLocale>
#include <QTranslator>
#include <QUrl>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-files"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    QApplication::setDesktopFileName(QStringLiteral("karyo-files"));
    Karyo::Ui::applyStyle();
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-files"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("select"), QStringLiteral("Show and select the requested items in their folders.")});
    parser.addOption({QStringLiteral("new-window"), QStringLiteral("Open a new window instead of reusing a folder tab.")});
    parser.addOption({QStringLiteral("dbus-service"), QStringLiteral("Wait for a FileManager1 request without opening Home.")});
    parser.addPositionalArgument(QStringLiteral("targets"), QStringLiteral("Folders, files or local file URLs."), QStringLiteral("[targets...]"));
    parser.process(app);
    QStringList targets;
    for (const QString &arg : parser.positionalArguments()) {
        QUrl url = QUrl::fromUserInput(arg, QDir::currentPath(), QUrl::AssumeLocalFile);
        if (url.isLocalFile() && (url.host().isEmpty() || url.host() == QLatin1String("localhost"))) {
            url.setHost(QString());
            if (!url.toLocalFile().contains(QChar::Null))
                targets.append(QDir::cleanPath(url.toLocalFile()));
        }
    }
    const bool server = parser.isSet(QStringLiteral("dbus-service"));
    if (server)
        QApplication::setQuitOnLastWindowClosed(false);
    const bool reveal = parser.isSet(QStringLiteral("select"));
    const bool newWindow = parser.isSet(QStringLiteral("new-window"));
    if (targets.isEmpty() && !server)
        targets.append(QDir::homePath());

    const QString activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    FilesService files;
    FileManagerService standard(&files);
    auto bus = QDBusConnection::sessionBus();
    const QString name = QStringLiteral("org.openkaryon.Files");
    const QString path = QStringLiteral("/org/openkaryon/Files");
    if (bus.isConnected()) {
        // Export objects before the names become visible to simultaneous starts.
        bus.registerObject(path, &files, QDBusConnection::ExportScriptableSlots);
        bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &standard,
                           QDBusConnection::ExportScriptableSlots);
        if (!bus.registerService(name)) {
            if (server)
                return 0;
            QDBusInterface running(name, path, name, bus);
            const QDBusReply<void> reply = running.call(QStringLiteral("Open"), targets, reveal, newWindow, activationToken);
            if (reply.isValid())
                return 0;
            qCritical() << "Cannot contact the running file manager:" << reply.error().message();
            return 1;
        }
        if (!bus.registerService(QStringLiteral("org.freedesktop.FileManager1")) && server) {
            qCritical() << "Cannot register FileManager1:" << bus.lastError().message();
            return 1;
        }
    } else if (server) {
        qCritical() << "FileManager1 requires the session bus.";
        return 1;
    }
    if (!server)
        files.Open(targets, reveal, newWindow, activationToken);
    return QApplication::exec();
}
