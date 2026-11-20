// SPDX-License-Identifier: GPL-3.0-or-later
// The XDG desktop folder as icons on the primary screen's wallpaper: open,
// select, rename, trash, paste and file drops, like a folder in Files.
#pragma once

#include <QColor>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QDropEvent;
class QLineEdit;
class QMimeData;

class DesktopIcons : public QWidget
{
    Q_OBJECT
public:
    explicit DesktopIcons(QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Item
    {
        QFileInfo info;
        QString label;   // elided to fit two lines
        QPixmap image;   // the type glyph, or a launcher's app icon
        QColor tile;
        bool launcher = false; // an executable .desktop file
    };

    // Rereads the folder; the watcher calls it through m_reload.
    void reload();
    QRect cellRect(int index) const;
    QRect labelRect(int index) const;
    int itemAt(const QPoint &pos) const;
    int indexOf(const QString &path) const;
    void paintTile(QPainter &p, const QRectF &tile, const Item &item) const;
    void updateItem(int index);
    void setSelection(const QSet<QString> &paths);
    QStringList selectedPaths() const;

    void open(const Item &item);
    void showMenu(const QPoint &globalPos, int index);
    void startRename(int index);
    void finishRename(bool commit);
    void trashSelection();
    void newFolder();
    void copySelection(bool cut);
    void paste();
    void startDrag(int index);
    // Whether a drop at pos is taken; target is the folder or launcher under
    // it, or -1 for the desktop folder.
    bool dropTarget(const QPoint &pos, const QMimeData *mime, bool ownDrag, int *target) const;
    Qt::DropAction dropAction(const QDropEvent *event, bool ownDrag) const;
    void copyOrMove(const QStringList &sources, const QString &targetDir, bool move);
    void warn(const QString &text);

    QString m_dir;
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
    QList<Item> m_items;
    QSet<QString> m_selected; // paths, so selection survives rereads
    int m_hover = -1;
    int m_dropTarget = -1;

    int m_pressed = -1;
    QPoint m_pressPos;
    bool m_banding = false;
    QRect m_band;
    QSet<QString> m_bandBase; // selection kept by a Ctrl rubber band

    QLineEdit *m_editor = nullptr;
    QString m_editing;
    QString m_pendingRename; // a new folder, renamed once it shows up
};
