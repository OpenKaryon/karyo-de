// SPDX-License-Identifier: GPL-3.0-or-later
#include "places.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/navigation.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>
#include <unistd.h>

namespace Ui = Karyo::Ui;

namespace {

const QString UDisks = QStringLiteral("org.freedesktop.UDisks2");
const QString UDisksPath = QStringLiteral("/org/freedesktop/UDisks2");
const QString Block = UDisks + QStringLiteral(".Block");
const QString Drive = UDisks + QStringLiteral(".Drive");
const QString Filesystem = UDisks + QStringLiteral(".Filesystem");
const QString ObjectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString MountPrefix = QStringLiteral(":mount:");
using Interfaces = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, Interfaces>;

QByteArray withoutTerminator(QByteArray value)
{
    if (value.endsWith('\0'))
        value.chop(1);
    return value;
}

QSettings filesSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("files"));
}

QString displayPath(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home)
        return QObject::tr("Home");
    return QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
}

} // namespace

// ---------------------------------------------------------------- Breadcrumb

Breadcrumb::Breadcrumb(QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
{
    setFixedHeight(34);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_edit->hide();
    m_edit->setFrame(false);
    m_edit->setFont(Ui::bodyFont(13));
    const Ui::Theme &t = Ui::Theme::current();
    QPalette p = m_edit->palette();
    p.setColor(QPalette::Base, Qt::transparent);
    // The subtle item highlight can blend into the breadcrumb's own fill.
    p.setColor(QPalette::Highlight, t.selInk);
    p.setColor(QPalette::HighlightedText, t.win);
    m_edit->setPalette(p);
    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        QString text = m_edit->text().trimmed();
        if (text.startsWith(QLatin1Char('~')))
            text = QDir::homePath() + text.mid(1);
        m_edit->hide();
        if (QFileInfo(text).isDir())
            Q_EMIT navigate(text);
        update();
    });
    connect(m_edit, &QLineEdit::editingFinished, this, [this] {
        m_edit->hide();
        update();
    });
}

void Breadcrumb::setPath(const QString &path)
{
    m_path = path;
    layoutSegments();
    update();
}

void Breadcrumb::editPath()
{
    m_edit->setGeometry(16, 0, width() - 32, height());
    m_edit->setText(m_path);
    m_edit->show();
    m_edit->setFocus();
    m_edit->selectAll();
    update();
}

void Breadcrumb::layoutSegments()
{
    m_segments.clear();
    const QString home = QDir::homePath();
    QString rest = m_path;
    QString base = QStringLiteral("/");
    if (m_path == home || m_path.startsWith(home + QLatin1Char('/'))) {
        m_segments.append({tr("Home"), home, {}});
        base = home;
        rest = m_path.mid(home.size());
    } else {
        m_segments.append({tr("Computer"), QStringLiteral("/"), {}});
    }
    QString current = base;
    for (const QString &part : rest.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        current = QDir(current).filePath(part);
        m_segments.append({part, current, {}});
    }
    // Lay out from the right; leading segments collapse when space runs out.
    const QFont font = Ui::bodyFont(13);
    const QFontMetrics fm(font);
    int x = 44;
    QList<int> widths;
    for (const Segment &s : std::as_const(m_segments))
        widths.append(fm.horizontalAdvance(s.label) + 20);
    int total = 0;
    for (int w : widths)
        total += w + 20;
    int first = 0;
    while (total > width() - 60 && first < m_segments.size() - 1) {
        total -= widths.at(first) + 20;
        ++first;
    }
    for (int i = 0; i < m_segments.size(); ++i) {
        if (i < first) {
            m_segments[i].rect = QRect();
            continue;
        }
        m_segments[i].rect = QRect(x, 4, widths.at(i), height() - 8);
        x += widths.at(i) + 20;
    }
}

void Breadcrumb::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    Ui::fillRounded(painter, rect(), height() / 2.0, t.card);
    if (m_edit->isVisible()) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(t.accent, 2));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), height() / 2.0 - 1, height() / 2.0 - 1);
        return;
    }
    const qreal dpr = devicePixelRatioF();
    painter.drawPixmap(16, (height() - 16) / 2, Ui::icon(QStringLiteral("house"), 16, t.muted, dpr));
    painter.setFont(Ui::bodyFont(13));
    bool firstShown = true;
    for (int i = 0; i < m_segments.size(); ++i) {
        const Segment &s = m_segments.at(i);
        if (s.rect.isNull())
            continue;
        painter.drawPixmap(s.rect.left() - 18, (height() - 12) / 2,
                           Ui::icon(QStringLiteral("chevron-right"), 12, t.muted, dpr));
        if (firstShown && i > 0) {
            painter.setPen(t.muted);
            painter.drawText(QRect(s.rect.left() - 34, 0, 14, height()), Qt::AlignCenter, QStringLiteral("…"));
        }
        firstShown = false;
        if (i == m_hover)
            Ui::fillRounded(painter, s.rect, s.rect.height() / 2.0, t.hover);
        const bool last = i == m_segments.size() - 1;
        painter.setFont(Ui::bodyFont(13, last ? QFont::DemiBold : QFont::Normal));
        painter.setPen(t.text);
        painter.drawText(s.rect, Qt::AlignCenter, s.label);
    }
}

void Breadcrumb::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint p = event->position().toPoint();
    if (p.x() < 40) {
        Q_EMIT navigate(QDir::homePath());
        return;
    }
    for (const Segment &s : std::as_const(m_segments)) {
        if (s.rect.contains(p)) {
            Q_EMIT navigate(s.path);
            return;
        }
    }
    editPath();
}

void Breadcrumb::mouseMoveEvent(QMouseEvent *event)
{
    int hover = -1;
    for (int i = 0; i < m_segments.size(); ++i) {
        if (m_segments.at(i).rect.contains(event->position().toPoint()))
            hover = i;
    }
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void Breadcrumb::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void Breadcrumb::resizeEvent(QResizeEvent *)
{
    layoutSegments();
    if (m_edit->isVisible())
        m_edit->setGeometry(16, 0, width() - 32, height());
}

// ---------------------------------------------------------------- Places

Places::Places(QWidget *parent)
    : QWidget(parent)
    , m_list(new Ui::NavList)
{
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_list);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    m_list->setAutoFillBackground(false);
    m_list->setRowHeight(30);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);

    QSettings settings = filesSettings();
    const QStringList defaults = {
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
    };
    m_pinned = settings.value(QStringLiteral("Places/pinned"), defaults).toStringList();

    connect(m_list, &Ui::NavList::activated, this, [this](int row) {
        const QString path = m_paths.value(row);
        if (path.startsWith(MountPrefix))
            mountUsb(path.mid(MountPrefix.size()));
        else if (!path.isEmpty())
            Q_EMIT navigate(path);
    });
    connect(m_list, &Ui::NavList::expanderToggled, this, [this](int row) {
        if (m_paths.value(row) == QLatin1String(":computer"))
            m_computerOpen = !m_computerOpen;
        else if (m_paths.value(row) == QLatin1String(":network"))
            m_networkOpen = !m_networkOpen;
        rebuild();
    });
    connect(m_list, &Ui::NavList::contextMenuRequested, this, [this](int row, const QPoint &pos) {
        const QString path = m_paths.value(row);
        if (!m_pinned.contains(path))
            return;
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        Ui::styleMenu(menu);
        menu->addAction(tr("Unpin from Quick access"), this, [this, path] { unpin(path); });
        menu->popup(pos);
    });

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Places::refreshMounts);
    timer->start(5000);

    qDBusRegisterMetaType<Interfaces>();
    qDBusRegisterMetaType<ManagedObjects>();
    auto bus = QDBusConnection::systemBus();
    bus.connect(UDisks, UDisksPath, ObjectManager, QStringLiteral("InterfacesAdded"), this, SLOT(refreshUsb()));
    bus.connect(UDisks, UDisksPath, ObjectManager, QStringLiteral("InterfacesRemoved"), this, SLOT(refreshUsb()));
    bus.connect(UDisks, {}, Properties, QStringLiteral("PropertiesChanged"),
                this, SLOT(usbPropertiesChanged(QString)));
    auto *service = new QDBusServiceWatcher(UDisks, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(service, &QDBusServiceWatcher::serviceOwnerChanged, this, &Places::refreshUsb);
    rebuild();
    refreshUsb();
}

QString Places::trashPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
}

void Places::pin(const QString &path)
{
    if (m_pinned.contains(path))
        return;
    m_pinned.append(path);
    QSettings settings = filesSettings();
    settings.setValue(QStringLiteral("Places/pinned"), m_pinned);
    rebuild();
}

void Places::unpin(const QString &path)
{
    m_pinned.removeAll(path);
    QSettings settings = filesSettings();
    settings.setValue(QStringLiteral("Places/pinned"), m_pinned);
    rebuild();
}

void Places::setCurrentPath(const QString &path)
{
    m_current = path;
    int best = -1;
    for (int i = 0; i < m_paths.size(); ++i) {
        if (m_paths.at(i) == path)
            best = i;
    }
    m_list->setCurrentIndex(best);
}

void Places::refreshMounts()
{
    if (isVisible())
        rebuild();
}

void Places::usbPropertiesChanged(const QString &interface)
{
    if (interface == Filesystem || interface == Block || interface == Drive)
        refreshUsb();
}

void Places::refreshUsb()
{
    if (m_usbRefreshPending) {
        m_usbRefreshAgain = true;
        return;
    }
    m_usbRefreshPending = true;
    const auto request = QDBusMessage::createMethodCall(UDisks, UDisksPath, ObjectManager,
                                                        QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<ManagedObjects> reply = *call;
        call->deleteLater();
        m_usbRefreshPending = false;
        m_usbVolumes.clear();
        const ManagedObjects objects = reply.isError() ? ManagedObjects{} : reply.value();
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            if (!it.value().contains(Filesystem))
                continue;
            const auto block = it.value().value(Block);
            if (block.value(QStringLiteral("HintIgnore")).toBool()
                || block.value(QStringLiteral("HintSystem")).toBool())
                continue;
            const auto drivePath = block.value(QStringLiteral("Drive")).value<QDBusObjectPath>();
            const auto drive = objects.value(drivePath).value(Drive);
            if (drive.value(QStringLiteral("ConnectionBus")).toString() != QLatin1String("usb"))
                continue;
            const auto device = withoutTerminator(block.value(QStringLiteral("Device")).toByteArray());
            const auto mounts = qdbus_cast<QList<QByteArray>>(
                it.value().value(Filesystem).value(QStringLiteral("MountPoints")));
            QString mountPoint;
            for (const QByteArray &mount : mounts) {
                const QString path = QFile::decodeName(withoutTerminator(mount));
                if (QFileInfo(path).isDir()) {
                    mountPoint = path;
                    break;
                }
            }
            // Ignore the live boot medium and mounts outside the user's view.
            if (mountPoint == QLatin1String("/") || mountPoint.startsWith(QLatin1String("/boot"))
                || mountPoint.startsWith(QLatin1String("/run/live/"))
                || mountPoint.startsWith(QLatin1String("/usr/lib/live/"))
                || (!mounts.isEmpty() && mountPoint.isEmpty()))
                continue;
            const QString label = block.value(QStringLiteral("IdLabel")).toString();
            m_usbVolumes.append({it.key().path(), device,
                                 label.isEmpty() ? tr("USB drive (%1)").arg(QFileInfo(QFile::decodeName(device)).fileName())
                                                 : label,
                                 mountPoint});
        }
        rebuild();
        if (std::exchange(m_usbRefreshAgain, false))
            refreshUsb();
    });
}

void Places::mountUsb(const QString &objectPath)
{
    if (m_mounting.contains(objectPath))
        return;
    m_mounting.insert(objectPath);
    rebuild();
    auto request = QDBusMessage::createMethodCall(UDisks, objectPath, Filesystem, QStringLiteral("Mount"));
    request.setInteractiveAuthorizationAllowed(true);
    request << QVariantMap{};
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(request, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, objectPath](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<QString> reply = *call;
        call->deleteLater();
        m_mounting.remove(objectPath);
        if (!reply.isError()) {
            for (UsbVolume &volume : m_usbVolumes) {
                if (volume.objectPath == objectPath)
                    volume.mountPoint = reply.value();
            }
            Q_EMIT navigate(reply.value());
        } else if (reply.error().name() != UDisks + QStringLiteral(".Error.AlreadyMounted")) {
            auto *message = new QMessageBox(QMessageBox::Warning, tr("Could not mount USB drive"),
                                            reply.error().message(), QMessageBox::Ok, this);
            message->setTextFormat(Qt::PlainText);
            message->setAttribute(Qt::WA_DeleteOnClose);
            message->open();
        }
        rebuild();
        refreshUsb();
    });
}

void Places::rebuild()
{
    // Rows are rebuilt in place, so the scroll position stays.
    QList<QString> paths;
    Ui::NavList *fresh = m_list;
    const QSignalBlocker blocker(fresh);
    fresh->clear();
    auto add = [&](const QString &glyph, const QString &label, const QString &path) {
        const int row = fresh->addItem(glyph, label);
        paths.append(path);
        return row;
    };
    auto separator = [&] {
        fresh->addSeparator();
        paths.append(QString());
    };

    add(QStringLiteral("house"), tr("Home"), QDir::homePath());
    add(QStringLiteral("image"), tr("Gallery"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    separator();
    for (const QString &path : std::as_const(m_pinned)) {
        if (!QFileInfo(path).isDir())
            continue;
        QString glyph = QStringLiteral("folder");
        if (path == QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
            glyph = QStringLiteral("monitor");
        else if (path == QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
            glyph = QStringLiteral("download");
        else if (path == QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            glyph = QStringLiteral("file-text");
        else if (path == QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            glyph = QStringLiteral("image");
        const int row = add(glyph, displayPath(path), path);
        fresh->setIndent(row, 0);
    }
    separator();

    const int computer = add(QStringLiteral("monitor"), tr("This Computer"), QStringLiteral(":computer"));
    fresh->setExpander(computer, true, m_computerOpen);
    QList<QStorageInfo> network;
    QSet<QByteArray> devices;
    QSet<QByteArray> usbDevices;
    QSet<QString> usbMounts;
    for (const UsbVolume &volume : std::as_const(m_usbVolumes)) {
        usbDevices.insert(volume.device);
        if (!volume.mountPoint.isEmpty())
            usbMounts.insert(volume.mountPoint);
    }
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
        // Bind mounts of single files (containers) and repeated mounts of one device are skipped.
        if (!volume.isValid() || !volume.isReady() || !QFileInfo(volume.rootPath()).isDir())
            continue;
        const QString type = QString::fromLatin1(volume.fileSystemType());
        const QString root = volume.rootPath();
        if (type.startsWith(QLatin1String("nfs")) || type == QLatin1String("cifs") || type == QLatin1String("smb3")
            || type == QLatin1String("fuse.sshfs")) {
            network.append(volume);
            continue;
        }
        if ((!volume.device().startsWith("/dev/") && root != QLatin1String("/")) || root.startsWith(QLatin1String("/boot"))
            || root.startsWith(QLatin1String("/snap")) || root.startsWith(QLatin1String("/run/live"))
            || root.startsWith(QLatin1String("/usr/lib/live")) || (volume.isReadOnly() && root != QLatin1String("/")))
            continue;
        if (usbDevices.contains(volume.device()) || usbMounts.contains(root))
            continue;
        if (devices.contains(volume.device()) && volume.rootPath() != QLatin1String("/"))
            continue;
        devices.insert(volume.device());
        if (!m_computerOpen)
            continue;
        const bool removable = root.startsWith(QLatin1String("/media/")) || root.startsWith(QLatin1String("/run/media/"));
        QString label = root == QLatin1String("/") ? tr("System (/)")
                        : removable ? (volume.displayName().isEmpty() ? QFileInfo(root).fileName() : volume.displayName())
                                    : QStringLiteral("%1 (%2)").arg(QFileInfo(root).fileName(), root);
        const int row = add(removable ? QStringLiteral("usb") : QStringLiteral("hard-drive"), label, root);
        fresh->setIndent(row, 1);
        if (volume.bytesTotal() > 0 && !removable) {
            const int used = int(100 * (volume.bytesTotal() - volume.bytesAvailable()) / volume.bytesTotal());
            fresh->setTrailing(row, QStringLiteral("%1%").arg(used));
        }
    }
    if (m_computerOpen) {
        for (const UsbVolume &volume : std::as_const(m_usbVolumes)) {
            const int row = add(QStringLiteral("usb"), volume.label, volume.mountPoint.isEmpty()
                                ? MountPrefix + volume.objectPath : volume.mountPoint);
            fresh->setIndent(row, 1);
            if (volume.mountPoint.isEmpty())
                fresh->setTrailing(row, m_mounting.contains(volume.objectPath) ? tr("Mounting…") : tr("Mount"));
        }
    }
    // gvfs mounts (sftp, smb) appear as folders under /run/user/UID/gvfs.
    const QString gvfs = QStringLiteral("/run/user/%1/gvfs").arg(getuid());
    const QFileInfoList gvfsMounts = QDir(gvfs).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    const int networkRow = add(QStringLiteral("network"), tr("Network"), QStringLiteral(":network"));
    fresh->setExpander(networkRow, true, m_networkOpen);
    if (m_networkOpen) {
        for (const QStorageInfo &volume : std::as_const(network)) {
            const int row = add(QStringLiteral("server"), QFileInfo(volume.rootPath()).fileName(), volume.rootPath());
            fresh->setIndent(row, 1);
            fresh->setTrailing(row, QString::fromLatin1(volume.fileSystemType()).remove(QStringLiteral("fuse.")));
        }
        for (const QFileInfo &mount : gvfsMounts) {
            const QString name = mount.fileName();
            const QString scheme = name.section(QLatin1Char(':'), 0, 0);
            const QString host = name.section(QStringLiteral("host="), 1).section(QLatin1Char(','), 0, 0);
            const int row = add(QStringLiteral("server"), host.isEmpty() ? name : host, mount.absoluteFilePath());
            fresh->setIndent(row, 1);
            fresh->setTrailing(row, scheme);
        }
    }
    separator();
    add(QStringLiteral("trash-2"), tr("Trash"), trashPath());

    m_paths = paths;
    setCurrentPath(m_current);
}
