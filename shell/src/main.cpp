// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-shell: wallpaper and taskbar of the Karyo desktop on wlroots
// compositors (labwc). Start, flyouts and notifications join this process.
#include "networkstatus.h"
#include "notify/notificationserver.h"
#include "polkit/polkitagent.h"
#include "quick/systemcontrols.h"
#include "sessionlocker.h"
#include "shellservice.h"
#include "start/appindex.h"
#include "taskbar.h"
#include "toplevels.h"
#include "tray/trayhost.h"
#include "wallpaper.h"

#include <karyo/ui/style.h>
#include <karyo/ui/theme.h>

#include <QApplication>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QTranslator>

#include <cstdio>

namespace {

// Settings > System > Display keeps modes and scales in desktop.conf
// [Displays]; wlr-randr applies them for this session.
void applySavedDisplays()
{
    QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                       QStringLiteral("desktop"));
    settings.beginGroup(QStringLiteral("Displays"));
    for (const QString &output : settings.childGroups()) {
        QStringList args{QStringLiteral("--output"), output};
        const QString mode = settings.value(output + QStringLiteral("/mode")).toString();
        const QString scale = settings.value(output + QStringLiteral("/scale")).toString();
        if (!mode.isEmpty())
            args << QStringLiteral("--mode") << mode;
        if (!scale.isEmpty())
            args << QStringLiteral("--scale") << scale;
        if (args.size() > 2)
            QProcess::startDetached(QStringLiteral("wlr-randr"), args);
    }
}

} // namespace

int main(int argc, char **argv)
{
    // "karyo-shell --start" (the Super key in labwc) asks the running
    // shell to toggle Start, without starting a GUI.
    if (argc == 2 && qstrcmp(argv[1], "--start") == 0) {
        QCoreApplication client(argc, argv);
        if (ShellService::call(QStringLiteral("ToggleStart")))
            return 0;
        std::fputs("karyo-shell: no running shell answered\n", stderr);
        return 1;
    }

    // Layer-shell surfaces only exist on Wayland.
    qputenv("QT_QPA_PLATFORM", "wayland");
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("karyo-shell"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    QApplication::setDesktopFileName(QStringLiteral("karyo-shell"));
    QApplication::setQuitOnLastWindowClosed(false);
    // Start and the flyouts hold scroll areas and menus: they use the Karyo style.
    Karyo::Ui::applyStyle();
    // Theme, accent, wallpaper and taskbar options change live from Settings.
    Karyo::Ui::Theme::followConfiguration();
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        std::fputs("karyo-shell: a Wayland session (labwc) is required\n", stderr);
        return 1;
    }

    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("karyo-shell"), QStringLiteral("_"),
                        QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    auto *toplevels = new ToplevelManager(&app);
    auto *tray = new TrayHost(&app);
    auto *network = new NetworkStatus(&app);
    auto *apps = new AppIndex(&app);
    auto *notifications = new NotificationServer(&app);
    if (!notifications->registerOnBus())
        std::fputs("karyo-shell: another notification daemon runs; notifications go there\n", stderr);
    auto *nightLight = new NightLight(&app);
    // Administrator prompts (pkexec, timedated, NetworkManager) come here.
    auto *polkit = new PolkitAgent(&app);
    if (!polkit->registerForSession())
        std::fputs("karyo-shell: another polkit agent serves this session\n", stderr);
    // "loginctl lock-session" and sleep lock the screen.
    new SessionLocker(&app);

    applySavedDisplays();

    // One wallpaper per screen; the taskbar and the desktop icons sit on the primary screen.
    QList<Wallpaper *> wallpapers;
    for (QScreen *screen : QGuiApplication::screens()) {
        auto *wallpaper = new Wallpaper(screen, screen == QGuiApplication::primaryScreen());
        wallpaper->show();
        wallpapers.append(wallpaper);
    }
    QObject::connect(&app, &QGuiApplication::screenAdded, &app, [](QScreen *screen) {
        (new Wallpaper(screen, false))->show();
    });

    Taskbar taskbar(QGuiApplication::primaryScreen(), toplevels, tray, network, apps, notifications, nightLight);
    taskbar.show();
    auto *service = new ShellService(&taskbar, notifications, &app);
    if (!service->registerOnBus())
        std::fputs("karyo-shell: org.karyo.Shell is taken; the Start key will not work\n", stderr);

    const int status = QApplication::exec();
    qDeleteAll(wallpapers);
    return status;
}
