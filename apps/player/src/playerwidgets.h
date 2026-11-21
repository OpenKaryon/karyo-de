// SPDX-License-Identifier: GPL-3.0-or-later
// Media Player's pieces in the Karyo look: the seek and volume bars, the
// round play button, the song list, the video grid, the video picture and
// the cover of what plays.
#pragma once

#include "library.h"

#include <QAbstractButton>
#include <QImage>
#include <QWidget>

class QVideoFrame;
class QVideoSink;

// A pill track with the accent fill up to the value; drag or click to set it.
class SeekBar : public QWidget
{
    Q_OBJECT
public:
    explicit SeekBar(QWidget *parent = nullptr);
    void setRange(qint64 maximum);
    void setValue(qint64 value);
    qint64 value() const { return m_value; }
    bool isDragging() const { return m_dragging; }

Q_SIGNALS:
    void moved(qint64 value);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    qint64 valueAt(int x) const;

    qint64 m_maximum = 0;
    qint64 m_value = 0;
    bool m_dragging = false;
};

// The accent circle with play or pause.
class PlayButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit PlayButton(QWidget *parent = nullptr);
    void setPlaying(bool playing);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_playing = false;
};

// Songs as rows: number, title, artist and album, format. The playing one
// is in the accent.
class TrackList : public QWidget
{
    Q_OBJECT
public:
    explicit TrackList(QWidget *parent = nullptr);
    void setItems(const QList<MediaItem> &items);
    const QList<MediaItem> &items() const { return m_items; }
    void setPlaying(const QString &path);
    QSize sizeHint() const override;

Q_SIGNALS:
    void activated(int index);
    void contextMenuRequested(int index, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int rowAt(const QPoint &pos) const;

    QList<MediaItem> m_items;
    QString m_playing;
    int m_hover = -1;
};

// Videos as cards: a dark 16:9 tile with the film glyph, name and size.
class VideoGrid : public QWidget
{
    Q_OBJECT
public:
    explicit VideoGrid(QWidget *parent = nullptr);
    void setItems(const QList<MediaItem> &items);
    const QList<MediaItem> &items() const { return m_items; }
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }
    QSize sizeHint() const override;

Q_SIGNALS:
    void activated(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect cardRect(int index, int width) const;
    int cardAt(const QPoint &pos) const;

    QList<MediaItem> m_items;
    int m_hover = -1;
};

// The playing video, letterboxed on black. Frames come from a QVideoSink and
// are painted here, so no GPU video surface is needed.
class VideoView : public QWidget
{
    Q_OBJECT
public:
    explicit VideoView(QWidget *parent = nullptr);
    QVideoSink *sink() const { return m_sink; }
    void clear();

Q_SIGNALS:
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void showFrame(const QVideoFrame &frame);

    QVideoSink *m_sink;
    QImage m_frame;
};

// The cover of the playing song, or a music tile when it has none.
class CoverArt : public QWidget
{
public:
    explicit CoverArt(int size, QWidget *parent = nullptr);
    void setImage(const QImage &image);
    // The glyph without a cover: music, or film for a video.
    void setGlyph(const QString &glyph);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    QString m_glyph = QStringLiteral("music");
};
