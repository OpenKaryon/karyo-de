// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QRect>
#include <QString>

struct CaptureWindow {
    QRect geometry;
    QString title;
};

// Topmost first. An empty list is valid when the desktop has no windows.
bool windowSnapshot(QList<CaptureWindow> *windows, QString *error);
