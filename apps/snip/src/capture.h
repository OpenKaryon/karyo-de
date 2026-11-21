// SPDX-License-Identifier: GPL-3.0-or-later
// Taking and keeping a screenshot: grim for each screen's pixels, the
// Pictures/Screenshots folder for the file, wl-copy for the clipboard (it
// keeps serving the picture after Snipping Tool exits) and a notification
// that opens the markup window.
#pragma once

#include <QImage>
#include <QObject>

class QScreen;

namespace Capture {
struct Frame {
    QRect geometry;  // global logical coordinates
    QImage pixels;
};
// Crop/join frozen outputs at the highest scale of the intersected screens.
QImage slice(const QList<Frame> &frames, const QRect &geometry);
// The screen's pixels, at its own resolution.
QImage grab(QScreen *screen, QString *error);
// ~/Pictures/Screenshots/Screenshot 2026-10-23 054210.png
QString newPath();
bool save(const QImage &image, const QString &path, QString *error);
// Puts the picture on the clipboard.
void copy(const QImage &image);
}

// "Screenshot copied": a notification with the picture; clicking it asks
// for the markup window. finished() comes when it is closed or forgotten.
class ScreenshotNotice : public QObject
{
    Q_OBJECT
public:
    ScreenshotNotice(const QString &path, QObject *parent = nullptr);

Q_SIGNALS:
    void markupRequested();
    void finished();

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &action);
    void onClosed(uint id, uint reason);

private:
    uint m_id = 0;
};
