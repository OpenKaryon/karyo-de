// SPDX-License-Identifier: GPL-3.0-or-later
// karyo-shell: wallpaper and taskbar of the Karyo desktop on wlroots
// compositors (labwc). Start, flyouts and notifications join this process.
#include "appearancepublisher.h"
#include "networkstatus.h"
#include "layersurface.h"
#include "notify/notificationserver.h"
#include "polkit/polkitagent.h"
#include "profilecontrol.h"
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
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QSaveFile>
#include <QScreen>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QSettings>
#include <QTranslator>

#include <cstdio>
#include <memory>

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
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(
            QString::fromLatin1(ShellService::ServiceName)).value())
        return 0;
    QApplication::setApplicationName(QStringLiteral("karyo-shell"));
    QApplication::setOrganizationName(QStringLiteral("openkaryon"));
    QApplication::setDesktopFileName(QStringLiteral("karyo-shell"));
    QApplication::setQuitOnLastWindowClosed(false);
    // Start and the flyouts hold scroll areas and menus: they use the Karyo style.
    Karyo::Ui::applyStyle();
    // Theme, accent, wallpaper and taskbar options change live from Settings.
    Karyo::Ui::Theme::followConfiguration();
    auto publishAppearance = []
    {
        Karyo::Session::publishAppearance(
            Karyo::Ui::Theme::schemeSetting(), Karyo::Ui::Theme::accentSetting());
    };
    publishAppearance();
    QObject::connect(Karyo::Ui::ThemeNotifier::instance(), &Karyo::Ui::ThemeNotifier::configurationChanged,
        &app, [publishAppearance](const QString &name)
        {
            if (name == QLatin1String("desktop"))
                publishAppearance();
        });
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
    auto *profiles = new ProfileControl(&app);
    // Administrator prompts (pkexec, timedated, NetworkManager) come here.
    auto *polkit = new PolkitAgent(&app);
    if (!polkit->registerForSession())
        std::fputs("karyo-shell: another polkit agent serves this session\n", stderr);
    // "loginctl lock-session" and sleep lock the screen.
    new SessionLocker(&app);

    applySavedDisplays();

    // Services and application models outlive transient output-bound windows.
    auto *service = new ShellService(nullptr, notifications, &app);
    if (!service->registerOnBus())
        std::fputs("karyo-shell: org.karyo.Shell is taken; the Start key will not work\n", stderr);

    QHash<QScreen *, Wallpaper *> wallpapers;
    QPointer<Taskbar> taskbar;
    QPointer<QScreen> panelScreen;
    QPointer<QScreen> iconsScreen;
    auto outputContext = std::make_unique<QObject>();
    const auto retirePanel = [&] {
        service->setTaskbar(nullptr);
        if (taskbar) {
            taskbar->hideForOutputLoss();
            taskbar->deleteLater();
            taskbar.clear();
        }
        panelScreen.clear();
    };
    const auto retireWallpaper = [&](QScreen *screen) {
        if (auto *view = wallpapers.take(screen)) {
            view->hide();
            view->deleteLater();
        }
    };
    const auto reconcile = [&] {
        QList<QScreen *> screens;
        for (auto *screen : QGuiApplication::screens()) {
            if (LayerSurface::outputAvailable(screen))
                screens.append(screen);
        }
        QScreen *primary = QGuiApplication::primaryScreen();
        if (!screens.contains(primary))
            primary = screens.value(0);
        // The desktop icons follow the real primary output, including when a
        // temporary Qt placeholder was the only screen during a VT change.
        const bool iconsMoved = iconsScreen != primary;
        for (auto *screen : wallpapers.keys()) {
            if (iconsMoved || !screens.contains(screen))
                retireWallpaper(screen);
        }
        iconsScreen = primary;
        for (auto *screen : screens) {
            if (!wallpapers.contains(screen)) {
                auto *view = new Wallpaper(screen, screen == primary);
                wallpapers.insert(screen, view);
                view->show();
            }
        }
        if (panelScreen != primary || (primary && !taskbar)) {
            retirePanel();
            if (primary) {
                taskbar = new Taskbar(primary, toplevels, tray, network, apps, notifications, nightLight, profiles);
                panelScreen = primary;
                service->setTaskbar(taskbar);
                taskbar->show();
            }
        }
    };
    bool scheduled = false;
    const auto schedule = [&] {
        if (scheduled)
            return;
        scheduled = true;
        QTimer::singleShot(0, outputContext.get(), [&] {
            scheduled = false;
            reconcile();
        });
    };
    QObject::connect(&app, &QGuiApplication::screenAdded, outputContext.get(), schedule);
    QObject::connect(&app, &QGuiApplication::primaryScreenChanged, outputContext.get(), schedule);
    QObject::connect(&app, &QGuiApplication::screenRemoved, outputContext.get(), [&](QScreen *screen) {
        if (panelScreen == screen)
            retirePanel();
        retireWallpaper(screen);
        schedule();
    });
    reconcile();
    const int status = QApplication::exec();
    outputContext.reset(); // Cancel callbacks before captured window state is destroyed.
    service->setTaskbar(nullptr);
    delete taskbar.data();
    qDeleteAll(wallpapers);
    return status;
}
