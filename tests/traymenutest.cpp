// SPDX-License-Identifier: GPL-3.0-or-later
#include "buttons.h"
#include "trayflyout.h"
#include "tray/trayhost.h"
#include "tray/trayitem.h"

#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QProcess>
#include <QPointer>
#include <QPushButton>
#include <QTextStream>
#include <QThread>
#include <functional>

struct MenuNode { int id = 0; QVariantMap props; QList<QDBusVariant> children; };
Q_DECLARE_METATYPE(MenuNode)
QDBusArgument &operator<<(QDBusArgument &arg, const MenuNode &node)
{
    arg.beginStructure(); arg << node.id << node.props << node.children; arg.endStructure(); return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, MenuNode &node)
{
    arg.beginStructure(); arg >> node.id >> node.props >> node.children; arg.endStructure(); return arg;
}

class TestMenu : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
public:
    QString actionLabel = QStringLiteral("Open test app");

public Q_SLOTS:
    bool AboutToShow(int) { return false; }
    uint GetLayout(int, int, const QStringList &, MenuNode &layout)
    {
        MenuNode disabled{2, {{QStringLiteral("label"), actionLabel},
                              {QStringLiteral("enabled"), false}}, {}};
        MenuNode action{1, {{QStringLiteral("label"), actionLabel}}, {}};
        layout = {0, {}, {QDBusVariant(QVariant::fromValue(disabled)),
                          QDBusVariant(QVariant::fromValue(action))}};
        return 1;
    }
    void Event(int id, const QString &event, const QDBusVariant &, uint)
    {
        if (event == QLatin1String("clicked"))
            QTextStream(stdout) << "CLICKED " << id << Qt::endl;
    }
};

class TestItem : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString IconName READ icon)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
public:
    QString kind;
    QString id() const { return kind; }
    QString title() const { return kind.isEmpty() ? QStringLiteral("Tray test") : kind; }
    QString status() const { return QStringLiteral("Active"); }
    QString icon() const { return QStringLiteral("dialog-information"); }
    QDBusObjectPath menu() const { return QDBusObjectPath(QStringLiteral("/Menu")); }
    bool itemIsMenu() const { return kind.isEmpty(); }

public Q_SLOTS:
    void Activate(int, int) { QTextStream(stdout) << "ACTIVATE" << Qt::endl; }
    void ContextMenu(int, int) { QTextStream(stdout) << "CONTEXT" << Qt::endl; }
};

bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 3000) {
        QApplication::processEvents();
        if (condition()) return true;
        QThread::msleep(10);
    }
    return false;
}

void mouse(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPoint &pos)
{
    QMouseEvent event(type, QPointF(pos), QPointF(widget->mapToGlobal(pos)), button,
                      type == QEvent::MouseButtonPress ? button : Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
    QApplication::processEvents();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    qDBusRegisterMetaType<MenuNode>();
    if (app.arguments().contains(QStringLiteral("--export"))) {
        auto bus = QDBusConnection::sessionBus();
        TestItem item; TestMenu menu;
        item.kind = app.arguments().value(2);
        if (item.kind.startsWith(QLatin1String("Claude_status_icon_")))
            menu.actionLabel = QStringLiteral("_Show App");
        else if (item.kind == QLatin1String("NetBird"))
            menu.actionLabel = QStringLiteral("Open NetBird");
        if (!bus.registerService(QStringLiteral("org.openkaryon.TrayRegression"))) return 2;
        bus.registerObject(QStringLiteral("/StatusNotifierItem"), &item, QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots);
        bus.registerObject(QStringLiteral("/Menu"), &menu, QDBusConnection::ExportAllSlots);
        QDBusInterface watcher(QStringLiteral("org.kde.StatusNotifierWatcher"), QStringLiteral("/StatusNotifierWatcher"),
                               QStringLiteral("org.kde.StatusNotifierWatcher"));
        watcher.asyncCall(QStringLiteral("RegisterStatusNotifierItem"), QStringLiteral("org.openkaryon.TrayRegression"));
        return app.exec();
    }
    TrayHost host;
    QProcess exporter;
    exporter.start(app.applicationFilePath(), {QStringLiteral("--export")});
    auto finish = [&exporter](bool ok, const char *message) {
        QTextStream(ok ? stdout : stderr) << message << Qt::endl;
        exporter.kill(); exporter.waitForFinished(); return ok ? 0 : 1;
    };
    if (!waitFor([&] { return !host.visibleItems().isEmpty()
        && host.visibleItems().first()->toolTipTitle() == QLatin1String("Tray test"); }))
        return finish(false, "FAIL: tray item did not register");
    QWidget taskbar;
    taskbar.resize(600, 60); taskbar.show();
    QPushButton anchor(&taskbar);
    anchor.setGeometry(400, 0, 40, 40); anchor.show();
    TrayFlyout flyout(&host, &taskbar);
    flyout.popupAbove(&anchor);
    QApplication::processEvents();
    auto *tile = flyout.findChild<ShellButton *>();
    if (!tile) return finish(false, "FAIL: tray tile missing");
    const QPoint pos = tile->rect().center();
    mouse(tile, QEvent::MouseButtonPress, Qt::RightButton, pos);
    if (!flyout.isVisible()) return finish(false, "FAIL: right press closed the tray before opening its menu");
    mouse(tile, QEvent::MouseButtonRelease, Qt::RightButton, pos);
    auto currentMenu = [&]() -> QMenu * {
        for (auto *menu : flyout.findChildren<QMenu *>()) if (menu->isVisible()) return menu;
        return nullptr;
    };
    if (!waitFor([&] { return flyout.isVisible() && currentMenu(); }))
        return finish(false, "FAIL: tray/menu did not stay open after right release");
    QPointer<QMenu> menu = currentMenu();
    // Offscreen has no native popup grab. Exercise action dispatch here; the
    // pointer/compositor path is additionally checked under headless labwc.
    menu->actions().last()->trigger();
    menu->close();
    QByteArray events;
    if (!waitFor([&] { events += exporter.readAllStandardOutput(); return events.contains("CLICKED 1"); }))
        return finish(false, "FAIL: choosing the menu action did not reach the tray application");
    if (!flyout.isVisible()) return finish(false, "FAIL: menu selection unexpectedly destroyed the tray");
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&flyout, &escape);
    if (flyout.isVisible()) return finish(false, "FAIL: Escape did not dismiss the tray");
    flyout.popupAbove(&anchor);
    QApplication::processEvents();
    tile = flyout.findChild<ShellButton *>();
    mouse(tile, QEvent::MouseButtonPress, Qt::LeftButton, tile->rect().center());
    mouse(tile, QEvent::MouseButtonRelease, Qt::LeftButton, tile->rect().center());
    if (!waitFor([&] { return flyout.isVisible() && currentMenu(); }))
        return finish(false, "FAIL: menu-only tray item closed its parent on left activation");
    const auto reload = [&](const QString &kind) {
        exporter.kill();
        exporter.waitForFinished();
        flyout.close();
        if (!waitFor([&] { return host.visibleItems().isEmpty(); }))
            return false;
        exporter.start(app.applicationFilePath(), {QStringLiteral("--export"), kind});
        if (!waitFor([&] { return !host.visibleItems().isEmpty()
            && host.visibleItems().first()->title() == kind; }))
            return false;
        flyout.popupAbove(&anchor);
        QApplication::processEvents();
        tile = flyout.findChild<ShellButton *>();
        return tile != nullptr;
    };
    const auto click = [&](QWidget *widget, Qt::MouseButton button, const QPoint &point) {
        mouse(widget, QEvent::MouseButtonPress, button, point);
        mouse(widget, QEvent::MouseButtonRelease, button, point);
    };
    for (const QString &kind : {QStringLiteral("Claude_status_icon_3"), QStringLiteral("NetBird"),
                                QStringLiteral("Mattermost_status_icon_1")}) {
        if (!reload(kind))
            return finish(false, "FAIL: replacement tray item did not register");
        click(tile, Qt::LeftButton, tile->rect().center());
        QByteArray output;
        const bool normal = kind.startsWith(QLatin1String("Mattermost"));
        if (!waitFor([&] { output += exporter.readAllStandardOutput(); return output.contains(normal ? "ACTIVATE" : "CLICKED 1"); })
            || output.contains("CLICKED 2") || (!normal && output.contains("ACTIVATE")))
            return finish(false, "FAIL: left click did not dispatch the expected enabled action");
    }

    if (!reload(QStringLiteral("nm-tray")))
        return finish(false, "FAIL: network tray item did not register");
    int networkRequests = 0;
    QObject::connect(&flyout, &TrayFlyout::networkRequested, &flyout, [&] { ++networkRequests; });
    click(tile, Qt::LeftButton, tile->rect().center());
    if (networkRequests != 1 || flyout.isVisible())
        return finish(false, "FAIL: network left click did not open the native panel");
    flyout.popupAbove(&anchor);
    click(tile, Qt::RightButton, tile->rect().center());
    if (networkRequests != 2 || flyout.isVisible())
        return finish(false, "FAIL: network right click did not open the native panel");

    QuickButton quick(&taskbar);
    quick.show();
    int requests = 0;
    QObject::connect(&quick, &QAbstractButton::clicked, &quick, [&] { ++requests; });
    click(&quick, Qt::LeftButton, QPoint(18, 20));
    click(&quick, Qt::LeftButton, QPoint(54, 20));
    click(&quick, Qt::RightButton, QPoint(18, 20));
    if (requests != 2 || quick.contextMenuPolicy() != Qt::CustomContextMenu)
        return finish(false, "FAIL: taskbar network/audio mouse routing");
    return finish(true, "PASS: context menu, app activation, and native network click routing");
}

#include "traymenutest.moc"
