// SPDX-License-Identifier: GPL-3.0-or-later
#include "capture.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QPainter>
#include <QSaveFile>

// Strings of the free functions, in the "Capture" context.
class CaptureText
{
    Q_DECLARE_TR_FUNCTIONS(Capture)
};

namespace Capture {

QImage slice(const QList<Frame> &frames, const QRect &geometry)
{
    QRect visible;
    qreal scale = 0;
    for (const Frame &frame : frames) {
        const QRect part = frame.geometry.intersected(geometry);
        if (part.isEmpty() || frame.pixels.isNull())
            continue;
        visible = visible.united(part);
        scale = qMax(scale, qreal(frame.pixels.width()) / frame.geometry.width());
        scale = qMax(scale, qreal(frame.pixels.height()) / frame.geometry.height());
    }
    if (visible.isEmpty())
        return {};
    QImage image(QSize(qCeil(visible.width() * scale), qCeil(visible.height() * scale)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const Frame &frame : frames) {
        const QRect part = frame.geometry.intersected(visible);
        if (part.isEmpty() || frame.pixels.isNull())
            continue;
        const qreal sx = qreal(frame.pixels.width()) / frame.geometry.width();
        const qreal sy = qreal(frame.pixels.height()) / frame.geometry.height();
        const QRectF source((part.x() - frame.geometry.x()) * sx, (part.y() - frame.geometry.y()) * sy,
                            part.width() * sx, part.height() * sy);
        const QRectF target((part.x() - visible.x()) * scale, (part.y() - visible.y()) * scale,
                            part.width() * scale, part.height() * scale);
        painter.drawImage(target, frame.pixels, source);
    }
    return image;
}

QImage grab(QScreen *screen, QString *error)
{
    QProcess grim;
    QStringList args;
    if (screen)
        args << QStringLiteral("-o") << screen->name();
    args << QStringLiteral("-t") << QStringLiteral("ppm") << QStringLiteral("-");
    grim.start(QStringLiteral("grim"), args);
    if (!grim.waitForFinished(10000) || grim.exitCode() != 0) {
        *error = grim.error() == QProcess::FailedToStart ? CaptureText::tr("grim is not installed")
                                                         : QString::fromLocal8Bit(grim.readAllStandardError()).trimmed();
        return {};
    }
    // PPM: no compression to undo, the fastest grim can hand over.
    QImage image;
    image.loadFromData(grim.readAllStandardOutput(), "PPM");
    if (image.isNull())
        *error = CaptureText::tr("grim gave no picture");
    return image;
}

QString newPath()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                           + QStringLiteral("/Screenshots");
    QDir().mkpath(folder);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss"));
    QString path = folder + QStringLiteral("/Screenshot %1.png").arg(stamp);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = folder + QStringLiteral("/Screenshot %1 (%2).png").arg(stamp).arg(n);
    return path;
}

bool save(const QImage &image, const QString &path, QString *error)
{
    QSaveFile file(path);
    const QByteArray format = QFileInfo(path).suffix().compare(QLatin1String("jpg"), Qt::CaseInsensitive) == 0
                            ? QByteArray("JPG") : QByteArray("PNG");
    if (file.open(QIODevice::WriteOnly) && image.save(&file, format.constData()) && file.commit())
        return true;
    *error = CaptureText::tr("“%1” could not be written").arg(QDir::toNativeSeparators(path));
    return false;
}

void copy(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    // wl-copy stays in the background and serves the picture until something
    // else is copied; Qt's clipboard would empty when this process ends.
    QProcess wlCopy;
    wlCopy.start(QStringLiteral("wl-copy"), {QStringLiteral("--type"), QStringLiteral("image/png")});
    if (wlCopy.waitForStarted(3000)) {
        const bool written = wlCopy.write(png) == png.size();
        wlCopy.closeWriteChannel();
        if (wlCopy.waitForFinished(3000) && written
            && wlCopy.exitStatus() == QProcess::NormalExit && wlCopy.exitCode() == 0)
            return;
        if (wlCopy.state() != QProcess::NotRunning) {
            wlCopy.kill();
            wlCopy.waitForFinished(1000);
        }
    }
    QGuiApplication::clipboard()->setImage(image);
}

} // namespace Capture

namespace {

const QString Service = QStringLiteral("org.freedesktop.Notifications");
const QString Path = QStringLiteral("/org/freedesktop/Notifications");

} // namespace

ScreenshotNotice::ScreenshotNotice(const QString &path, QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(Service, Path, Service, QStringLiteral("ActionInvoked"), this, SLOT(onActionInvoked(uint, QString)));
    bus.connect(Service, Path, Service, QStringLiteral("NotificationClosed"), this, SLOT(onClosed(uint, uint)));
    QDBusInterface notifications(Service, Path, Service, bus);
    const QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("karyo-snip")},
                            {QStringLiteral("image-path"), path}};
    const QDBusReply<uint> reply = notifications.call(
        QStringLiteral("Notify"), tr("Snipping Tool"), 0u, QStringLiteral("karyo-snip"), tr("Screenshot copied"),
        tr("Saved to Pictures › Screenshots. Select to mark it up."),
        QStringList{QStringLiteral("default"), tr("Mark up")}, hints, -1);
    if (!reply.isValid()) {
        QTimer::singleShot(0, this, &ScreenshotNotice::finished);
        return;
    }
    m_id = reply.value();
    // A notice nobody answers lets the process go after a while; the
    // picture stays on the clipboard through wl-copy.
    QTimer::singleShot(5 * 60 * 1000, this, &ScreenshotNotice::finished);
}

void ScreenshotNotice::onActionInvoked(uint id, const QString &)
{
    if (id == m_id)
        Q_EMIT markupRequested();
}

void ScreenshotNotice::onClosed(uint id, uint)
{
    if (id == m_id)
        Q_EMIT finished();
}
