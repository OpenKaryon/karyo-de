// SPDX-License-Identifier: GPL-3.0-or-later
#include "imageview.h"

#include <QImageReader>
#include <QImageWriter>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QSaveFile>
#include <QWheelEvent>

#include <cmath>

namespace {

// Pictures larger than this along an edge are first decoded smaller.
constexpr int PreviewEdge = 2560;

} // namespace

ImageView::ImageView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

bool ImageView::open(const QString &path)
{
    delete m_movie;
    m_path = path;
    m_error.clear();
    m_image = QImage();
    m_rotation = 0;
    m_pan = {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (reader.supportsAnimation() && reader.imageCount() > 1) {
        auto *movie = new QMovie(path, QByteArray(), this);
        connect(movie, &QMovie::frameChanged, this, [this, movie] {
            m_image = movie->currentImage();
            update();
        });
        m_movie = movie;
        movie->start();
        m_image = movie->currentImage();
        m_original = m_image.size();
        m_full = true;
    } else {
        m_original = reader.size();
        // The EXIF orientation turns the size too.
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            m_original.transpose();
        if (m_original.isValid() && qMax(m_original.width(), m_original.height()) > PreviewEdge) {
            QSize preview = reader.size();
            preview.scale(PreviewEdge, PreviewEdge, Qt::KeepAspectRatio);
            reader.setScaledSize(preview);
            m_full = false;
        } else {
            m_full = true;
        }
        m_image = reader.read();
        if (m_image.isNull()) {
            m_error = reader.errorString();
            update();
            return false;
        }
        if (!m_original.isValid())
            m_original = m_image.size();
    }
    fit();
    return true;
}

QSize ImageView::turnedSize() const
{
    return m_rotation % 2 ? m_original.transposed() : m_original;
}

void ImageView::fit()
{
    const QSize size = turnedSize();
    if (size.isEmpty() || width() <= 0) {
        m_scale = 1.0;
    } else {
        // Small pictures stay at their own size, as Windows Photos shows them.
        const qreal sx = qreal(width() - 32) / size.width();
        const qreal sy = qreal(height() - 32) / size.height();
        m_scale = qMin(1.0, qMin(sx, sy));
    }
    m_fitted = true;
    m_pan = {};
    update();
    Q_EMIT zoomChanged(m_scale);
}

void ImageView::zoomBy(qreal factor) { zoomAround(m_scale * factor, rect().center()); }

void ImageView::zoomTo(qreal scale) { zoomAround(scale, rect().center()); }

void ImageView::zoomAround(qreal scale, const QPointF &anchor)
{
    if (m_original.isEmpty())
        return;
    scale = qBound(0.02, scale, 16.0);
    // The point under the anchor stays under it.
    const QPointF center = QPointF(width(), height()) / 2 + m_pan;
    const QPointF fromCenter = (anchor - center) / m_scale;
    m_scale = scale;
    m_pan = anchor - fromCenter * m_scale - QPointF(width(), height()) / 2;
    m_fitted = false;
    clampPan();
    ensureResolution();
    update();
    Q_EMIT zoomChanged(m_scale);
}

void ImageView::ensureResolution()
{
    if (m_full || m_movie || m_image.isNull())
        return;
    // More screen pixels than decoded ones: read the whole picture now.
    if (m_scale * m_original.width() > m_image.width() * 1.05) {
        QImageReader reader(m_path);
        reader.setAutoTransform(true);
        const QImage full = reader.read();
        if (!full.isNull()) {
            m_image = full;
            m_full = true;
        }
    }
}

void ImageView::clampPan()
{
    const QSizeF shown = QSizeF(turnedSize()) * m_scale;
    const qreal maxX = qMax(0.0, (shown.width() - width()) / 2);
    const qreal maxY = qMax(0.0, (shown.height() - height()) / 2);
    m_pan.setX(qBound(-maxX, m_pan.x(), maxX));
    m_pan.setY(qBound(-maxY, m_pan.y(), maxY));
}

void ImageView::rotate(int quarterTurns)
{
    m_rotation = ((m_rotation + quarterTurns) % 4 + 4) % 4;
    fit();
}

bool ImageView::saveRotation(QString *error)
{
    if (m_rotation == 0 || m_movie)
        return true;
    QImageReader reader(m_path);
    reader.setAutoTransform(true);
    QImage full = reader.read();
    if (full.isNull()) {
        *error = reader.errorString();
        return false;
    }
    full = full.transformed(QTransform().rotate(90 * m_rotation), Qt::SmoothTransformation);
    QSaveFile output(m_path);
    if (!output.open(QIODevice::WriteOnly)) {
        *error = output.errorString();
        return false;
    }
    QImageWriter writer(&output, reader.format());
    writer.setQuality(95);
    if (!writer.write(full)) {
        *error = writer.errorString();
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        *error = output.errorString();
        return false;
    }
    return open(m_path);
}

void ImageView::resizeEvent(QResizeEvent *)
{
    if (m_fitted)
        fit();
    else
        clampPan();
}

void ImageView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_image.isNull())
        return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 2.0);
    painter.translate(QPointF(width(), height()) / 2 + m_pan);
    painter.rotate(90 * m_rotation);
    const QSizeF shown = QSizeF(m_original) * m_scale;
    painter.drawImage(QRectF(-shown.width() / 2, -shown.height() / 2, shown.width(), shown.height()), m_image);
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    const qreal steps = event->angleDelta().y() / 120.0;
    if (steps == 0)
        return;
    zoomAround(m_scale * std::pow(1.15, steps), event->position());
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragFrom = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
    }
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    const QPoint now = event->position().toPoint();
    m_pan += now - m_dragFrom;
    m_dragFrom = now;
    clampPan();
    update();
}

void ImageView::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
    unsetCursor();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Fitted -> one to one where clicked, and back.
    if (m_fitted)
        zoomAround(1.0, event->position());
    else
        fit();
    Q_EMIT doubleClicked();
}
