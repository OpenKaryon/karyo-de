// SPDX-License-Identifier: GPL-3.0-or-later
// Tray overflow: background apps that are running but not pinned, in a
// 3-column glass card above the chevron.
#pragma once

#include <QList>
#include <QWidget>

class QGridLayout;
class TrayHost;

class TrayFlyout : public QWidget
{
    Q_OBJECT
public:
    TrayFlyout(TrayHost *host, QWidget *parent);

    // Shows the card centered on anchor, 60px above the anchor's bottom edge.
    void popupAbove(QWidget *anchor);

Q_SIGNALS:
    void networkRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void rebuild();
    QRect cardRect() const;

    TrayHost *m_host;
    QGridLayout *m_grid;
    QList<QWidget *> m_tiles;
};
