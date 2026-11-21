// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-player [FILE|FOLDER...]: Karyo Media Player. One window: a
// second start hands its files to the running player over D-Bus.
#include "playerwindow.h"

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
    // The player never encodes, and Qt's probe of hardware encoders opens a
    // Vulkan device: lavapipe and LLVM (30 MB) where there is no Vulkan GPU,
    // VMs among them. Decoding keeps VA-API and VDPAU.
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"))
        qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", "");
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "vaapi,vdpau");
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-player"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    // Public Wayland identity; Karyo supplies client-side decorations.
    QApplication::setDesktopFileName(QStringLiteral("karyo-player"));
    Karyo::Ui::applyStyle();

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-player"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    QStringList paths;
    for (const QString &arg : QApplication::arguments().mid(1)) {
        if (!arg.startsWith(QLatin1Char('-')))
            paths.append(QDir::current().absoluteFilePath(arg));
    }

    const QString service = QStringLiteral("org.openkaryon.Player");
    const QString path = QStringLiteral("/org/openkaryon/Player");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && !bus.registerService(service)) {
        QDBusInterface running(service, path, service, bus);
        if (QDBusReply<void>(running.call(QStringLiteral("Open"), paths)).isValid())
            return 0;
    }

    PlayerWindow window;
    if (bus.isConnected())
        bus.registerObject(path, new PlayerService(&window), QDBusConnection::ExportScriptableSlots);
    if (!paths.isEmpty())
        window.openPaths(paths);
    window.show();
    return QApplication::exec();
}
