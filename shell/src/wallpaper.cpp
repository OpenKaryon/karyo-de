// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallpaper.h"

#include "desktopicons.h"
#include "layersurface.h"

#include <karyo/ui/theme.h>

#include <QImageReader>
#include <QPainter>
#include <QScreen>
#include <QSettings>

namespace Ui = Karyo::Ui;

Wallpaper::Wallpaper(QScreen *screen, bool icons)
    : QWidget(nullptr, Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setWindowTitle(QStringLiteral("karyo-wallpaper"));
    LayerSurface::configure(this, screen, LayerSurface::LayerBackground,
                            LayerSurface::AnchorTop | LayerSurface::AnchorBottom
                                | LayerSurface::AnchorLeft | LayerSurface::AnchorRight,
                            -1);
    resize(screen->size());
    if (icons) {
        // A click on the desktop gives it the keyboard for F2, Delete and Ctrl+C/V.
        LayerSurface::setKeyboard(this, LayerSurface::KeyboardOnDemand);
        m_icons = new DesktopIcons(this);
        m_icons->setGeometry(rect());
    }
    connect(Ui::ThemeNotifier::instance(), &Ui::ThemeNotifier::changed, this,
            qOverload<>(&QWidget::update));
    connect(Ui::ThemeNotifier::instance(), &Ui::ThemeNotifier::configurationChanged, this,
            [this](const QString &name) {
                if (name == QLatin1String("desktop"))
                    readOptions();
            });
    readOptions();
}

void Wallpaper::readOptions()
{
    const QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                             QStringLiteral("desktop"));
    m_mode = settings.value(QStringLiteral("Background/mode"), QStringLiteral("organic")).toString();
    m_color = QColor(settings.value(QStringLiteral("Background/color")).toString());
    m_fill = settings.value(QStringLiteral("Background/fill"), QStringLiteral("fill")).toString();
    const QString path = settings.value(QStringLiteral("Background/picture")).toString();
    if (m_mode != QLatin1String("picture")) {
        m_picture = QImage();
        m_picturePath.clear();
    } else if (path != m_picturePath || m_picture.isNull()) {
        m_picturePath = path;
        m_picture = QImage();
    }
    if (m_mode == QLatin1String("picture") && m_picture.isNull() && size().isValid()) {
        // Decoded at screen size: a camera picture would otherwise hold tens of MB.
        QImageReader reader(m_picturePath);
        reader.setAutoTransform(true);
        const QSize source = reader.size();
        if (source.isValid()) {
            // fill covers the screen, fit shows all of it, center keeps the
            // picture's own size unless it is larger than the screen.
            QSize target = source.scaled(size(), m_fill == QLatin1String("fill") ? Qt::KeepAspectRatioByExpanding
                                                                                   : Qt::KeepAspectRatio);
            if (m_fill == QLatin1String("center") && source.width() <= width() && source.height() <= height())
                target = source;
            reader.setScaledSize(target);
            m_picture = reader.read();
        }
    }
    update();
}

void Wallpaper::resizeEvent(QResizeEvent *)
{
    if (m_icons)
        m_icons->setGeometry(rect());
    if (m_mode == QLatin1String("picture")) {
        m_picture = QImage();
        readOptions();
    }
}

void Wallpaper::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    if (m_mode == QLatin1String("solid") && m_color.isValid()) {
        p.fillRect(rect(), m_color);
        return;
    }
    p.fillRect(rect(), t.desk);
    if (m_mode == QLatin1String("picture") && !m_picture.isNull()) {
        // Fill crops to the screen, fit and center leave the desk around it.
        const QPoint at((width() - m_picture.width()) / 2, (height() - m_picture.height()) / 2);
        p.drawImage(at, m_picture);
        return;
    }
    paintShapes(p);
}

void Wallpaper::paintShapes(QPainter &p)
{
    const Ui::Theme &t = Ui::Theme::current();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    // Positions of KaryonShell.dc.html at 1920x1080, scaled with the screen height
    // and kept anchored to the same corners.
    const qreal s = height() / 1080.0;
    const qreal w = width();
    const qreal h = height();
    auto fromBottomLeft = [&](qreal left, qreal bottom, qreal size) {
        return QRectF(left * s, h - (bottom + size) * s, size * s, size * s);
    };
    auto fromTopRight = [&](qreal right, qreal top, qreal size) {
        return QRectF(w - (right + size) * s, top * s, size * s, size * s);
    };
    auto fromTopLeft = [&](qreal left, qreal top, qreal size) {
        return QRectF(left * s, top * s, size * s, size * s);
    };

    p.setBrush(t.blobA);
    p.drawEllipse(fromBottomLeft(-300, -460, 1180));
    p.setBrush(t.blobB);
    p.drawEllipse(fromBottomLeft(250, 40, 420));
    p.setBrush(t.blobE);
    p.drawEllipse(fromBottomLeft(470, 250, 110));
    p.setBrush(t.blobD);
    p.drawEllipse(fromTopRight(-180, -220, 700));

    // 180px ring with a 30px border.
    const QRectF ring = fromTopRight(420, 330, 180);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(t.blobC, 30 * s));
    p.drawEllipse(ring.adjusted(15 * s, 15 * s, -15 * s, -15 * s));
    p.setPen(Qt::NoPen);

    p.setBrush(t.blobE);
    p.drawEllipse(fromTopRight(300, 560, 26));
    p.setBrush(t.blobB);
    p.drawEllipse(fromTopRight(250, 610, 14));
    p.setBrush(t.blobA);
    p.drawEllipse(fromTopLeft(820, 140, 18));
}
