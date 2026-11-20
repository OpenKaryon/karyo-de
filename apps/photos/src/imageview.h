// SPDX-License-Identifier: GPL-3.0-or-later
// One picture, fitted to the view or zoomed around the pointer and dragged.
// A large photo is decoded at screen size first and at its own size only
// when the zoom needs those pixels, so a 24-megapixel JPEG does not hold
// 100 MB until someone looks closely. Animated GIF and WebP play.
#pragma once

#include <QImage>
#include <QPointer>
#include <QWidget>

class QMovie;

class ImageView : public QWidget
{
    Q_OBJECT
public:
    explicit ImageView(QWidget *parent = nullptr);

    // False when the file cannot be read; error() says why.
    bool open(const QString &path);
    QString path() const { return m_path; }
    QString error() const { return m_error; }
    // The picture's own size (after its EXIF orientation).
    QSize pictureSize() const { return m_original; }

    // 1.0 shows the picture's pixels one to one.
    qreal scale() const { return m_scale; }
    bool isFitted() const { return m_fitted; }
    void fit();
    void zoomBy(qreal factor);
    void zoomTo(qreal scale);
    // Quarter turns, shown at once; rotation() != 0 means saving would change the file.
    void rotate(int quarterTurns);
    int rotation() const { return m_rotation; }
    // Writes the turned picture over the file.
    bool saveRotation(QString *error);

Q_SIGNALS:
    void zoomChanged(qreal scale);
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QSize turnedSize() const;
    void zoomAround(qreal scale, const QPointF &anchor);
    void clampPan();
    void ensureResolution();

    QString m_path;
    QString m_error;
    QImage m_image;       // decoded, at most the picture's size
    QSize m_original;
    bool m_full = false;  // m_image has every pixel
    QPointer<QMovie> m_movie;
    qreal m_scale = 1.0;
    bool m_fitted = true;
    int m_rotation = 0;   // quarter turns clockwise
    QPointF m_pan;        // offset of the picture's center from the view's
    QPoint m_dragFrom;
    bool m_dragging = false;
};
