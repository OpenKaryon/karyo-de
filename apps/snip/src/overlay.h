// SPDX-License-Identifier: GPL-3.0-or-later
// The snipping overlay, like Windows' Win+Shift+S: every screen frozen and
// dimmed on the overlay layer, a pill with Rectangle and Full screen at the
// top, and the dragged rectangle shown bright with its size. Escape leaves.
#pragma once

#include <QImage>
#include <QObject>
#include <QWidget>
#include "capture.h"
#include "windowsnapshot.h"

class QScreen;
class Snipper;

class SnipOverlay : public QWidget
{
    Q_OBJECT
public:
    SnipOverlay(Snipper *snipper, QScreen *screen, const QImage &frozen);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QImage cut(const QRect &area) const;

    Snipper *m_snipper;
    QImage m_frozen;
    QWidget *m_toolbar;
    QPoint m_from;
    QRect m_selection;
    bool m_selecting = false;
    QScreen *m_screen;
};

// Freezes every screen, shows the overlays and reports the chosen picture.
class Snipper : public QObject
{
    Q_OBJECT
public:
    explicit Snipper(QObject *parent = nullptr);
    // False when no screen could be grabbed; error() says why.
    bool start(bool windowMode = false);
    QString error() const { return m_error; }

    void finish(const QImage &image);
    void cancel();
    bool windowMode() const { return m_windowMode; }
    CaptureWindow windowAt(const QPoint &global) const;
    void finishWindow(const QPoint &global);
    void selectRegion(const QRect &global);
    void finishRegion();
    QRect selection(QScreen *screen) const;

Q_SIGNALS:
    void captured(const QImage &image);
    void cancelled();

private:
    void closeOverlays();

    QList<SnipOverlay *> m_overlays;
    QString m_error;
    bool m_windowMode = false;
    QList<CaptureWindow> m_windows;
    QList<Capture::Frame> m_frames;
    QRect m_selection;
};
