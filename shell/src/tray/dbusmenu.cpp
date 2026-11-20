// SPDX-License-Identifier: GPL-3.0-or-later
#include "dbusmenu.h"

#include <karyo/ui/menu.h>

#include <QAction>
#include <QActionGroup>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QDateTime>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QPointer>

namespace {

const QString MenuInterface = QStringLiteral("com.canonical.dbusmenu");
constexpr int CallTimeoutMs = 1500;

struct Node
{
    int id = 0;
    QVariantMap props;
    QList<Node> children;
};

Node readNode(const QDBusArgument &arg)
{
    // (ia{sv}av): id, properties, children wrapped in variants.
    Node node;
    arg.beginStructure();
    arg >> node.id;
    arg >> node.props;
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusVariant child;
        arg >> child;
        const QVariant value = child.variant();
        if (value.canConvert<QDBusArgument>())
            node.children.append(readNode(value.value<QDBusArgument>()));
    }
    arg.endArray();
    arg.endStructure();
    return node;
}

QString label(const QVariantMap &props)
{
    // dbusmenu uses '_' for mnemonics, Qt uses '&'.
    QString text = props.value(QStringLiteral("label")).toString();
    text.replace(QLatin1String("&"), QLatin1String("&&"));
    QString out;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('_')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('_')) {
                out.append(QLatin1Char('_'));
                ++i;
            } else {
                out.append(QLatin1Char('&'));
            }
        } else {
            out.append(text.at(i));
        }
    }
    return out;
}

void sendEvent(const QString &service, const QString &path, int id, const QString &event)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, MenuInterface,
                                                      QStringLiteral("Event"));
    call << id << event << QVariant::fromValue(QDBusVariant(0))
         << uint(QDateTime::currentSecsSinceEpoch());
    QDBusConnection::sessionBus().asyncCall(call);
}

void fill(QMenu *menu, const Node &node, const QString &service, const QString &path)
{
    QActionGroup *radioGroup = nullptr;
    for (const Node &child : node.children) {
        const QVariantMap &p = child.props;
        if (!p.value(QStringLiteral("visible"), true).toBool())
            continue;
        if (p.value(QStringLiteral("type")).toString() == QLatin1String("separator")) {
            menu->addSeparator();
            radioGroup = nullptr;
            continue;
        }
        QAction *action = nullptr;
        if (!child.children.isEmpty()
            || p.value(QStringLiteral("children-display")).toString() == QLatin1String("submenu")) {
            QMenu *sub = menu->addMenu(label(p));
            Karyo::Ui::styleMenu(sub);
            fill(sub, child, service, path);
            action = sub->menuAction();
        } else {
            action = menu->addAction(label(p));
            const int id = child.id;
            QObject::connect(action, &QAction::triggered, menu,
                             [service, path, id] { sendEvent(service, path, id, QStringLiteral("clicked")); });
        }
        action->setEnabled(p.value(QStringLiteral("enabled"), true).toBool());
        const QString iconName = p.value(QStringLiteral("icon-name")).toString();
        if (!iconName.isEmpty()) {
            action->setIcon(QIcon::fromTheme(iconName));
        } else if (const QByteArray png = p.value(QStringLiteral("icon-data")).toByteArray();
                   !png.isEmpty()) {
            QPixmap pixmap;
            if (pixmap.loadFromData(png))
                action->setIcon(QIcon(pixmap));
        }
        const QString toggle = p.value(QStringLiteral("toggle-type")).toString();
        if (!toggle.isEmpty()) {
            action->setCheckable(true);
            action->setChecked(p.value(QStringLiteral("toggle-state")).toInt() == 1);
            if (toggle == QLatin1String("radio")) {
                if (!radioGroup)
                    radioGroup = new QActionGroup(menu);
                radioGroup->addAction(action);
            }
        } else {
            radioGroup = nullptr;
        }
    }
}

Node layout(const QString &service, const QString &path)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage aboutToShow = QDBusMessage::createMethodCall(service, path, MenuInterface,
                                                             QStringLiteral("AboutToShow"));
    aboutToShow << 0;
    bus.call(aboutToShow, QDBus::Block, CallTimeoutMs);

    QDBusMessage getLayout = QDBusMessage::createMethodCall(service, path, MenuInterface,
                                                           QStringLiteral("GetLayout"));
    getLayout << 0 << -1 << QStringList();
    const QDBusMessage reply = bus.call(getLayout, QDBus::Block, CallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
        return {};
    return readNode(reply.arguments().at(1).value<QDBusArgument>());
}

} // namespace

namespace DBusMenu {

bool activateAction(const QString &service, const QString &path, const QString &name)
{
    const Node root = layout(service, path);
    for (const Node &child : root.children) {
        const auto &props = child.props;
        QString caption = props.value(QStringLiteral("label")).toString();
        caption.remove(QLatin1Char('_'));
        if (caption.compare(name, Qt::CaseInsensitive) == 0
            && props.value(QStringLiteral("visible"), true).toBool()
            && props.value(QStringLiteral("enabled"), true).toBool()
            && props.value(QStringLiteral("type")).toString() != QLatin1String("separator")
            && props.value(QStringLiteral("children-display")).toString() != QLatin1String("submenu")
            && child.children.isEmpty()) {
            sendEvent(service, path, child.id, QStringLiteral("clicked"));
            return true;
        }
    }
    return false;
}

void popup(const QString &service, const QString &path, const QPoint &globalPos, QWidget *parent)
{
    const Node root = layout(service, path);
    if (root.children.isEmpty())
        return;

    auto *menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Karyo::Ui::styleMenu(menu);
    fill(menu, root, service, path);
    QObject::connect(menu, &QMenu::aboutToHide, menu,
                     [service, path] { sendEvent(service, path, 0, QStringLiteral("closed")); });
    sendEvent(service, path, 0, QStringLiteral("opened"));
    menu->popup(globalPos);
}

} // namespace DBusMenu
