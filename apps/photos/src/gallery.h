// SPDX-License-Identifier: GPL-3.0-or-later
// The gallery: every picture under a folder (Pictures by default), newest
// first, grouped by month in a grid of square tiles; and the filmstrip under
// the viewer with the pictures next to the open one.
#pragma once

#include <QDateTime>
#include <QWidget>

class Thumbnails;

struct Picture
{
    QString path;
    QDateTime modified;
};

class GalleryGrid : public QWidget
{
    Q_OBJECT
public:
    GalleryGrid(Thumbnails *thumbnails, QWidget *parent = nullptr);

    // Newest first; the grid groups them by month.
    void setPictures(const QList<Picture> &pictures);
    const QList<Picture> &pictures() const { return m_pictures; }
    QSize sizeHint() const override;
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }

Q_SIGNALS:
    void activated(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Item
    {
        QRect rect;
        int picture = -1;  // -1: a month heading
        QString heading;
    };
    QList<Item> layoutItems(int width) const;
    int itemAt(const QPoint &pos) const;

    Thumbnails *m_thumbnails;
    QList<Picture> m_pictures;
    QList<Item> m_items;
    int m_hover = -1;
};

class Filmstrip : public QWidget
{
    Q_OBJECT
public:
    Filmstrip(Thumbnails *thumbnails, QWidget *parent = nullptr);
    void setPictures(const QStringList &paths, int current);
    void setCurrent(int index);

Q_SIGNALS:
    void activated(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QRect tileRect(int index) const;
    int tileAt(const QPoint &pos) const;
    void scrollToCurrent();

    Thumbnails *m_thumbnails;
    QStringList m_paths;
    int m_current = -1;
    int m_hover = -1;
    int m_offset = 0;
};
