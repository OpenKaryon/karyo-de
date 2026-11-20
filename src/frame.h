// SPDX-License-Identifier: GPL-3.0-or-later
// Window frame pieces shared by KaryoWindow and the client frame of other
// apps' windows (clientframe.cpp). Internal to the library.
#pragma once

#include <QWidget>

class QEvent;

namespace Karyo::Ui::detail {

constexpr qreal WindowRadius = 8;

// A title strip: dragging it moves the window, a double click maximizes.
class TitleBar : public QWidget
{
public:
    explicit TitleBar(QWidget *parent)
        : QWidget(parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
};

// Resizing a frameless window from its edges, for an application-wide event
// filter: shows the edge cursors over the window's widgets and starts a
// compositor resize on press. Returns true when it consumed the event.
bool filterResize(QWidget *window, QObject *watched, QEvent *event);

} // namespace Karyo::Ui::detail
