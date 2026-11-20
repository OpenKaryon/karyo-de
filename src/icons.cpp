// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/icons.h"

#include <QFile>
#include <QResource>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QtMath>

static void initializeResources()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(karyo_lucide);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace Karyo::Ui {

namespace {

QString resourcePath(const QString &name)
{
    initializeResources();
    return QStringLiteral(":/karyo/icons/lucide/%1.svg").arg(name);
}

const QByteArray &source(const QString &name)
{
    static QHash<QString, QByteArray> sources;
    auto it = sources.find(name);
    if (it == sources.end()) {
        QFile file(resourcePath(name));
        it = sources.insert(name, file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
    }
    return it.value();
}

} // namespace

bool hasIcon(const QString &name) { return !source(name).isEmpty(); }

QPixmap icon(const QString &name, int size, const QColor &color, qreal devicePixelRatio,
             qreal strokeWidth)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1/%2/%3/%4/%5")
                            .arg(name)
                            .arg(size)
                            .arg(color.name(QColor::HexArgb))
                            .arg(devicePixelRatio)
                            .arg(strokeWidth);
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return it.value();

    QByteArray svg = source(name);
    if (svg.isEmpty())
        return {};
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    static const QRegularExpression stroke(QStringLiteral("stroke-width=\"[^\"]*\""));
    svg = QString::fromUtf8(svg)
              .replace(stroke, QStringLiteral("stroke-width=\"%1\"").arg(strokeWidth))
              .toUtf8();
    if (color.alpha() != 255)
        svg.replace("<svg ", QByteArray("<svg opacity=\"") + QByteArray::number(color.alphaF())
                                 + "\" ");

    const int pixels = qCeil(size * devicePixelRatio);
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, pixels, pixels));
    painter.end();
    pixmap.setDevicePixelRatio(devicePixelRatio);
    cache.insert(key, pixmap);
    return pixmap;
}

} // namespace Karyo::Ui
