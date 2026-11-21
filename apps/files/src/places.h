// SPDX-License-Identifier: GPL-3.0-or-later
// Breadcrumb bar and the Places sidebar of Files.
#pragma once

#include <QList>
#include <QSet>
#include <QWidget>

class QLineEdit;

namespace Karyo::Ui {
class NavList;
}

// Home icon and path segments in a 40px pill; a click on empty space edits the path.
class Breadcrumb : public QWidget
{
    Q_OBJECT
public:
    explicit Breadcrumb(QWidget *parent = nullptr);
    void setPath(const QString &path);
    void editPath();

Q_SIGNALS:
    void navigate(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Segment
    {
        QString label;
        QString path;
        QRect rect;
    };
    void layoutSegments();

    QString m_path;
    QList<Segment> m_segments;
    int m_hover = -1;
    QLineEdit *m_edit;
};

// Home, Gallery, Quick access, This Computer, Network and Trash.
class Places : public QWidget
{
    friend class PlacesTest;
    Q_OBJECT
public:
    explicit Places(QWidget *parent = nullptr);
    void setCurrentPath(const QString &path);
    void refreshMounts();
    QStringList pinned() const { return m_pinned; }
    void pin(const QString &path);
    void unpin(const QString &path);
    static QString trashPath();

Q_SIGNALS:
    void navigate(const QString &path);

private Q_SLOTS:
    void refreshUsb();
    void usbPropertiesChanged(const QString &interface);

private:
    struct UsbVolume {
        QString objectPath;
        QByteArray device;
        QString label;
        QString mountPoint;
    };

    void mountUsb(const QString &objectPath);
    void rebuild();

    Karyo::Ui::NavList *m_list;
    QList<QString> m_paths;  // per row; empty for separators and headings
    QStringList m_pinned;
    QList<UsbVolume> m_usbVolumes;
    QSet<QString> m_mounting;
    bool m_usbRefreshPending = false;
    bool m_usbRefreshAgain = false;
    bool m_computerOpen = true;
    bool m_networkOpen = true;
    QString m_current;
};
