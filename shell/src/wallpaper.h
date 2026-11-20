// SPDX-License-Identifier: GPL-3.0-or-later
// Desktop background: flat --k-desk ground with the design's soft circles
// ("organic shapes"), a solid color, or a picture, as chosen in Settings.
// The primary screen's wallpaper also carries the desktop folder's icons.
#pragma once

#include <QImage>
#include <QWidget>

class DesktopIcons;
class QScreen;

class Wallpaper : public QWidget
{
    Q_OBJECT
public:
    Wallpaper(QScreen *screen, bool icons);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Rereads [Background] from desktop.conf.
    void readOptions();
    void paintShapes(QPainter &p);

    QString m_mode;   // organic, solid, picture
    QColor m_color;
    QString m_fill;   // fill, fit, center
    QString m_picturePath;
    QImage m_picture; // already scaled for the screen
    DesktopIcons *m_icons = nullptr;
};
