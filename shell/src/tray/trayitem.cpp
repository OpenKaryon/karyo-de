// SPDX-License-Identifier: GPL-3.0-or-later
#include "trayitem.h"

#include "dbusmenu.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include <QRegularExpression>
#include <QtEndian>

namespace {

const QString ItemInterface = QStringLiteral("org.kde.StatusNotifierItem");

// a(iiay): ARGB32 images in network byte order.
QIcon iconFromPixmaps(const QDBusArgument &argument)
{
    QIcon icon;
    argument.beginArray();
    while (!argument.atEnd()) {
        int width = 0;
        int height = 0;
        QByteArray data;
        argument.beginStructure();
        argument >> width >> height >> data;
        argument.endStructure();
        if (width <= 0 || height <= 0 || data.size() < qsizetype(width) * height * 4)
            continue;
        QImage image(width, height, QImage::Format_ARGB32);
        const auto *source = reinterpret_cast<const uchar *>(data.constData());
        for (int y = 0; y < height; ++y) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < width; ++x)
                row[x] = qFromBigEndian<quint32>(source + (qsizetype(y) * width + x) * 4);
        }
        icon.addPixmap(QPixmap::fromImage(image));
    }
    argument.endArray();
    return icon;
}

QString plainText(QString text)
{
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    text.replace(QLatin1String("<br>"), QLatin1String(" "), Qt::CaseInsensitive);
    text.remove(tags);
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    return text.simplified();
}

} // namespace

TrayItem::TrayItem(const QString &id, QObject *parent)
    : QObject(parent)
    , m_id(id)
{
    const qsizetype slash = id.indexOf(QLatin1Char('/'));
    m_service = slash > 0 ? id.left(slash) : id;
    m_path = slash > 0 ? id.mid(slash) : QStringLiteral("/StatusNotifierItem");

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(40);
    connect(&m_refreshTimer, &QTimer::timeout, this, &TrayItem::refresh);

    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const char *signal : {"NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon",
                               "NewToolTip", "NewStatus", "NewMenu"}) {
        bus.connect(m_service, m_path, ItemInterface, QString::fromLatin1(signal), this,
                    SLOT(scheduleRefresh()));
    }
    refresh();
}

void TrayItem::scheduleRefresh() { m_refreshTimer.start(); }

void TrayItem::refresh()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        m_service, m_path, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("GetAll"));
    call << ItemInterface;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError())
            return;
        const QVariantMap props = reply.value();
        m_applicationId = props.value(QStringLiteral("Id")).toString();
        m_title = props.value(QStringLiteral("Title")).toString();
        m_status = props.value(QStringLiteral("Status")).toString();
        m_iconName = props.value(QStringLiteral("IconName")).toString();
        m_attentionIconName = props.value(QStringLiteral("AttentionIconName")).toString();
        m_iconThemePath = props.value(QStringLiteral("IconThemePath")).toString();
        m_itemIsMenu = props.value(QStringLiteral("ItemIsMenu")).toBool();
        m_menuPath = props.value(QStringLiteral("Menu")).value<QDBusObjectPath>().path();
        if (m_menuPath == QLatin1String("/"))
            m_menuPath.clear();

        m_iconPixmaps = QIcon();
        if (const QVariant v = props.value(QStringLiteral("IconPixmap")); v.canConvert<QDBusArgument>())
            m_iconPixmaps = iconFromPixmaps(v.value<QDBusArgument>());
        m_attentionPixmaps = QIcon();
        if (const QVariant v = props.value(QStringLiteral("AttentionIconPixmap"));
            v.canConvert<QDBusArgument>())
            m_attentionPixmaps = iconFromPixmaps(v.value<QDBusArgument>());

        m_toolTipTitle.clear();
        m_toolTipText.clear();
        if (const QVariant v = props.value(QStringLiteral("ToolTip")); v.canConvert<QDBusArgument>()) {
            // (s a(iiay) s s): icon name, icon pixmaps, title, description.
            const QDBusArgument arg = v.value<QDBusArgument>();
            QString iconName;
            arg.beginStructure();
            arg >> iconName;
            iconFromPixmaps(arg);
            arg >> m_toolTipTitle >> m_toolTipText;
            arg.endStructure();
            // QSystemTrayIcon sends "title\nstatus" as the title alone.
            if (m_toolTipText.isEmpty() && m_toolTipTitle.contains(QLatin1Char('\n'))) {
                m_toolTipText = m_toolTipTitle.section(QLatin1Char('\n'), 1);
                m_toolTipTitle = m_toolTipTitle.section(QLatin1Char('\n'), 0, 0);
            }
            m_toolTipTitle = plainText(m_toolTipTitle);
            m_toolTipText = plainText(m_toolTipText);
        }
        Q_EMIT changed();
    });
}

QIcon TrayItem::iconFromName(const QString &name) const
{
    if (name.isEmpty())
        return {};
    if (QDir::isAbsolutePath(name))
        return QIcon(name);
    if (!m_iconThemePath.isEmpty()) {
        for (const char *ext : {".png", ".svg"}) {
            const QString path = QDir(m_iconThemePath).filePath(name + QLatin1String(ext));
            if (QFile::exists(path))
                return QIcon(path);
        }
        QStringList paths = QIcon::themeSearchPaths();
        if (!paths.contains(m_iconThemePath)) {
            paths.append(m_iconThemePath);
            QIcon::setThemeSearchPaths(paths);
        }
    }
    return QIcon::fromTheme(name);
}

QIcon TrayItem::icon() const
{
    if (m_status == QLatin1String("NeedsAttention")) {
        QIcon attention = iconFromName(m_attentionIconName);
        if (attention.isNull())
            attention = m_attentionPixmaps;
        if (!attention.isNull())
            return attention;
    }
    QIcon named = iconFromName(m_iconName);
    return named.isNull() ? m_iconPixmaps : named;
}

QString TrayItem::toolTipTitle() const
{
    return m_toolTipTitle.isEmpty() ? m_title : m_toolTipTitle;
}

void TrayItem::activate(const QPoint &globalPos, QWidget *parentWidget)
{
    // These applets expose opening their main window as a menu action.
    QString openAction;
    if (m_applicationId.startsWith(QLatin1String("Claude_status_icon_")))
        openAction = QStringLiteral("Show App");
    else if (m_applicationId == QLatin1String("NetBird"))
        openAction = QStringLiteral("Open NetBird");
    if (!openAction.isEmpty() && !m_menuPath.isEmpty()
        && DBusMenu::activateAction(m_service, m_menuPath, openAction))
        return;
    if (m_itemIsMenu && !m_menuPath.isEmpty()) {
        showContextMenu(globalPos, parentWidget);
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, ItemInterface,
                                                      QStringLiteral("Activate"));
    call << globalPos.x() << globalPos.y();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    QPointer<QWidget> parent(parentWidget);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, globalPos, parent](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                // libappindicator items have no Activate; they only offer their menu.
                if (w->isError() && !m_menuPath.isEmpty() && parent)
                    showContextMenu(globalPos, parent);
            });
}

void TrayItem::secondaryActivate(const QPoint &globalPos)
{
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, ItemInterface,
                                                      QStringLiteral("SecondaryActivate"));
    call << globalPos.x() << globalPos.y();
    QDBusConnection::sessionBus().asyncCall(call);
}

void TrayItem::showContextMenu(const QPoint &globalPos, QWidget *parentWidget)
{
    if (!m_menuPath.isEmpty()) {
        DBusMenu::popup(m_service, m_menuPath, globalPos, parentWidget);
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, m_path, ItemInterface,
                                                      QStringLiteral("ContextMenu"));
    call << globalPos.x() << globalPos.y();
    QDBusConnection::sessionBus().asyncCall(call);
}
